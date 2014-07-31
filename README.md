## alloc_track

Tracks the number of outstanding allocations on a Ruby thread using the internal tracepoint APIs.

### Features

- C extension for webscale
- Allocations are counted per thread
- Can raise an exception when allocations exceed a certain threshold

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

### Performance

In a contrived benchmark that simply allocates 10,000,000 new objects, alloc_track adds ~30% overhead:

```
[vagrant] ~/src/alloc_track (master *%) $ ruby -Ilib ./test/benchmark_alloc_track.rb
               user     system      total        real
none:      1.540000   0.000000   1.540000 (  1.545192)
tracking:  2.020000   0.000000   2.020000 (  2.034162)
```

Expect real-world (operations other than just memory allocation) performance overhead to be much less severe.

### Limitations

- Allocation tracker can only be run on one thread per process
