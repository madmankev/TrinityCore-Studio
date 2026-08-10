#pragma once

#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace wowedit
{
// Event types intentionally map one-to-one to the public editor contract.
enum class EventType
{
    // Terrain events
    TerrainModified,
    TexturePainted,
    WaterLevelChanged,

    // Object events
    DoodadPlaced,
    DoodadSelected,
    DoodadMoved,
    DoodadDeleted,
    DoodadTransformed,

    // Creature events
    CreaturePlaced,
    CreatureModified,
    WaypointAdded,

    // Selection events
    SelectionChanged,
    MultiSelectionChanged,

    // File events
    MapOpened,
    MapSaved,
    MapClosed,

    // View events
    CameraMoved,
    ViewportResized,
    DisplayModeChanged,

    // Tool events
    ToolActivated,
    ToolDeactivated,
    BrushSettingsChanged,

    // System events
    UndoPerformed,
    RedoPerformed,
    SettingsChanged
};

struct Event
{
    EventType type = EventType::SettingsChanged;
    std::uint64_t sequence = 0;
    std::string source;
    std::any payload;
};

/** Thread-safe publish/subscribe bus. Publishing invokes a snapshot of handlers,
 * so handlers may safely subscribe or unsubscribe while processing an event. */
class EventBus
{
public:
    using SubscriptionId = std::uint64_t;
    using Handler = std::function<void(const Event&)>;

    SubscriptionId subscribe(EventType type, Handler handler);
    void unsubscribe(SubscriptionId id);
    void publish(Event event);
    void enqueue(Event event);
    std::size_t dispatchQueued(std::size_t maxEvents = static_cast<std::size_t>(-1));
    void clear();

private:
    struct Subscription
    {
        SubscriptionId id = 0;
        Handler handler;
    };

    std::mutex mutex_;
    std::map<EventType, std::vector<Subscription>> handlers_;
    std::queue<Event> queued_;
    std::atomic<std::uint64_t> nextSubscription_{1};
    std::atomic<std::uint64_t> nextSequence_{1};
};
} // namespace wowedit
