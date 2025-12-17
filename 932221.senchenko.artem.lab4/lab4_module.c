#include <linux/module.h>      
#include <linux/kernel.h>     
#include <linux/init.h>      
#include <linux/proc_fs.h>     
#include <linux/seq_file.h>   
#include <linux/timekeeping.h>
#include <linux/time64.h>     

#define PROC_NAME "tsulab"


#define TUNGUSKA_TIME ((time64_t)-1943796180)


static int tsulab_show(struct seq_file *m, void *v)
{
    struct timespec64 now;     // текущее время
    time64_t hours_passed;     // сколько часов прошло
    time64_t mirrored_time;    // K - N
    struct tm tm_result;       // для форматирования

    // текущее реальное время 
    ktime_get_real_ts64(&now);

    // Сколько секунд прошло с момента падения
    hours_passed = (now.tv_sec - TUNGUSKA_TIME) / 3600;

    // Вычисляем момент времени K - N часов 
    mirrored_time = TUNGUSKA_TIME - hours_passed * 3600;

    // Перевод в удобный формат 
    time64_to_tm(mirrored_time, 0, &tm_result);

    // Вывод в /proc 
    seq_printf(m,
        "Tunguska meteorite time (K): 1908-06-30 07:17:00\n"
        "Hours passed since K (N): %lld\n"
        "Mirrored time (K - N): %04ld-%02d-%02d %02d:%02d:%02d\n",
        hours_passed,
        tm_result.tm_year + 1900,
        tm_result.tm_mon + 1,
        tm_result.tm_mday,
        tm_result.tm_hour,
        tm_result.tm_min,
        tm_result.tm_sec
    );

    return 0;
}



static int tsulab_open(struct inode *inode, struct file *file)  //Обёртка для seq_file
{
    return single_open(file, tsulab_show, NULL);
}


  //Описание операций файла /proc/tsulab
 
static const struct proc_ops tsulab_ops = {
    .proc_open    = tsulab_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};



 
static int __init lab4_init(void)  //Инициализация модуля
{
    proc_create(PROC_NAME, 0, NULL, &tsulab_ops);
    printk(KERN_INFO "Lab4 module loaded, /proc/tsulab created\n");
    return 0;
}


//Выгрузка модуля
static void __exit lab4_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "Lab4 module unloaded, /proc/tsulab removed\n");
}

module_init(lab4_init);
module_exit(lab4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Artem");
MODULE_DESCRIPTION("TSU Lab 4: Tunguska meteorite time calculation");
