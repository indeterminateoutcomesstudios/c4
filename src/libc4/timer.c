#include "c4-internal.h"
#include "router.h"
#include "timer.h"

typedef struct AlarmState
{
    apr_interval_time_t period;
    apr_interval_time_t deadline;
    TableDef *tbl_def;
    struct AlarmState *next;
} AlarmState;

struct C4Timer
{
    apr_pool_t *pool;
    C4Runtime *c4;
    AlarmState *alarm;
};

C4Timer *
timer_make(C4Runtime *c4)
{
    C4Timer *timer;

    timer = apr_palloc(c4->pool, sizeof(*timer));
    timer->pool = c4->pool;
    timer->c4 = c4;
    timer->alarm = NULL;
    return timer;
}

static apr_time_t
get_deadline(apr_time_t now, apr_interval_time_t delta)
{
    if (APR_INT64_MAX - now < delta)
        ERROR("Integer overflow: %" APR_TIME_T_FMT " + %" APR_TIME_T_FMT,
              now, delta);

    return now + delta;
}

void
timer_add_alarm(C4Timer *timer, const char *name, apr_int64_t period_msec)
{
    AlarmState *alarm;

    alarm = apr_palloc(timer->pool, sizeof(*alarm));
    alarm->period = period_msec * 1000;
    alarm->deadline = get_deadline(apr_time_now(), alarm->period);
    alarm->tbl_def = cat_get_table(timer->c4->cat, name);
    alarm->next = timer->alarm;
    timer->alarm = alarm;
}

/*
 * Return the approximate time before the next alarm will fire, or -1 if there
 * are no active alarms.
 */
apr_interval_time_t
timer_get_sleep_time(C4Timer *timer)
{
    apr_time_t min_deadline;
    apr_time_t now;
    AlarmState *alarm;

    if (timer->alarm == NULL)
        return -1;

    min_deadline = APR_INT64_MAX;
    now = apr_time_now();
    alarm = timer->alarm;
    while (alarm != NULL)
    {
        /* If we should have fired the alarm already, don't sleep */
        if (alarm->deadline <= now)
            return 0;

        if (alarm->deadline < min_deadline)
            min_deadline = alarm->deadline;

        alarm = alarm->next;
    }

    ASSERT(min_deadline > now);
    ASSERT(min_deadline < APR_INT64_MAX);
    return min_deadline - now;
}

static void
fire_alarm(AlarmState *alarm, C4Timer *timer)
{
    Datum time_val;
    Tuple *alarm_tuple;

    time_val.i8 = alarm->deadline;
    alarm_tuple = tuple_make(alarm->tbl_def->schema, &time_val);
    router_insert_tuple(timer->c4->router, alarm_tuple,
                        alarm->tbl_def, false);
    tuple_unpin(alarm_tuple, alarm->tbl_def->schema);

    alarm->deadline += alarm->period;
}

bool
timer_poll(C4Timer *timer)
{
    apr_time_t now;
    AlarmState *alarm;
    bool fired_alarm;

    fired_alarm = false;
    now = apr_time_now();
    alarm = timer->alarm;
    while (alarm != NULL)
    {
        /* NB: we might fire many times before advancing to the next alarm */
        if (alarm->deadline <= now)
        {
            fire_alarm(alarm, timer);
            fired_alarm = true;
        }
        else
            alarm = alarm->next;
    }

    return fired_alarm;
}
