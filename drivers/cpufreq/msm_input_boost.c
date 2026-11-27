/* msm_input_boost driver */
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/atomic.h>

#define LP_BOOST_FREQ 1209600
#define KRYO_BOOST_FREQ 1132800
#define BOOST_DURATION 60

static atomic_t is_boost = ATOMIC_INIT(0);
static u64 touch_ns;

static struct work_struct boost;
static struct delayed_work uboost;
static struct notifier_block cpu_notify;

static void update_cpus(void) {
        get_online_cpus();
        if (cpu_online(0))
            cpufreq_update_policy(0);
        if (cpu_online(6))
            cpufreq_update_policy(6);
        put_online_cpus();
}

static int cpu_freq_callback(struct notifier_block *nb, unsigned long val, void *data) {
    struct cpufreq_policy *policy = data;
    unsigned int boost_freq;
    
    if (val != CPUFREQ_ADJUST)
        return NOTIFY_OK;

    if (!atomic_read(&is_boost)) {
        if (policy->cpu >= 6)
            policy->min = policy->cpuinfo.min_freq;
        return NOTIFY_OK;
    }
    
    boost_freq = (policy->cpu < 6) ? LP_BOOST_FREQ : KRYO_BOOST_FREQ;

    policy->min = min(max(policy->min, boost_freq), policy->max);

    return NOTIFY_OK;
}

static void sboost_work(struct work_struct *work) {
    if (!atomic_read(&is_boost)) {
        atomic_set(&is_boost, 1);
        update_cpus();
    }
    mod_delayed_work(system_power_efficient_wq, &uboost, msecs_to_jiffies(BOOST_DURATION));
}

static void uboost_work(struct work_struct *work) {
    if (atomic_read(&is_boost)) {
        atomic_set(&is_boost, 0);
        update_cpus();
    }
}

static void touch_input(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    u64 now;

    if (type == EV_KEY && value == 0)
        return;

    now = ktime_to_ns(ktime_get());

    if (now - touch_ns < 25 * NSEC_PER_MSEC)
        return;

    touch_ns = now;
    queue_work(system_power_efficient_wq, &boost);
}

static int touch_con(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;
    int err;

    handle = kzalloc(sizeof(*handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "msm_input_boost";

    if ((err = input_register_handle(handle)))
        goto err_free;
    if ((err = input_open_device(handle)))
        goto err_unreg;

    return 0;

err_unreg:
    input_unregister_handle(handle);
err_free:
    kfree(handle);
    return err;        
}

static void touch_remove(struct input_handle *handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id touch_ids[] = {
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_ABS) } },
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_KEY) } },
    { },
};

static struct input_handler touch_handler = {
    .event = touch_input,
    .connect = touch_con,
    .disconnect = touch_remove,
    .name = "msm_input_boost",
    .id_table = touch_ids,
};

static int __init msm_input_boost_init(void) {
    INIT_WORK(&boost, sboost_work);
    INIT_DELAYED_WORK(&uboost, uboost_work);

    cpu_notify.notifier_call = cpu_freq_callback;
    cpufreq_register_notifier(&cpu_notify, CPUFREQ_POLICY_NOTIFIER);
    return input_register_handler(&touch_handler);
}
late_initcall(msm_input_boost_init);