## alloc_track

Tracks the number of outstanding allocations on a Ruby thread using the internal tracepoint APIs.

### Features

- C extension for webscale
- Allocations are only counted on the thread that invoked the tracker

### Usage

It can be used to track the number of allocated objects over a period of time:

```ruby
require 'alloc_track/alloc_track'

AllocTrack.start
100.times { Object.new }
puts AllocTrack.delta # >= 100
GC.start
puts AllocTrack.alloc # >= 100
puts AllocTrack.delta # <= 100
puts AllocTrack.free  # >= 100
AllocTrack.stop
```

Perhaps more useful is the ability to raise when the number of allocations crosses a certain threshold:
```ruby
require 'alloc_track/alloc_track'

AllocTrack.limit 100 do
	200.times { Object.new } # raises AllocTrack::LimitExceeded
end
```

