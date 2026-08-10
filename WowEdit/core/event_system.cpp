#include "core/event_system.h"

#include <algorithm>
#include <utility>

namespace wowedit
{
EventBus::SubscriptionId EventBus::subscribe(EventType type, Handler handler)
{
    if (!handler)
        return 0;

    const SubscriptionId id = nextSubscription_.fetch_add(1);
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[type].push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(SubscriptionId id)
{
    if (id == 0)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : handlers_)
    {
        std::vector<Subscription>& subscriptions = pair.second;
        subscriptions.erase(std::remove_if(subscriptions.begin(), subscriptions.end(),
                                           [id](const Subscription& subscription) { return subscription.id == id; }),
                            subscriptions.end());
    }
}

void EventBus::publish(Event event)
{
    event.sequence = event.sequence == 0 ? nextSequence_.fetch_add(1) : event.sequence;

    std::vector<Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = handlers_.find(event.type);
        if (found != handlers_.end())
            snapshot = found->second;
    }

    for (const Subscription& subscription : snapshot)
        if (subscription.handler)
            subscription.handler(event);
}

void EventBus::enqueue(Event event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queued_.push(std::move(event));
}

std::size_t EventBus::dispatchQueued(std::size_t maxEvents)
{
    std::size_t count = 0;
    while (count < maxEvents)
    {
        Event event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queued_.empty())
                break;
            event = std::move(queued_.front());
            queued_.pop();
        }
        publish(std::move(event));
        ++count;
    }
    return count;
}

void EventBus::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.clear();
    std::queue<Event> empty;
    queued_.swap(empty);
}
} // namespace wowedit
