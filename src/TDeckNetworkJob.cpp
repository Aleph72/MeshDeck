#include <atomic>
static std::atomic<int> owner{0};
extern "C" bool tdeck_network_job_acquire(int id)
{
    int empty = 0;
    return owner.compare_exchange_strong(empty, id);
}
extern "C" void tdeck_network_job_release(int id)
{
    owner.compare_exchange_strong(id, 0);
}
extern "C" bool tdeck_network_job_busy(void)
{
    return owner.load() != 0;
}
