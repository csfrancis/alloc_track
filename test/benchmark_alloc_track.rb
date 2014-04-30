require 'benchmark'
require 'alloc_track/alloc_track'

def alloc_obj
  Object.new
end

n = 100
i = 100000

Benchmark.bm(8) do |x|
  x.report("none:") do
    GC.start
    n.times do
      i.times { alloc_obj }
      GC.start
    end
  end
  x.report("tracking:") do
    GC.start
    AllocTrack.start
    n.times do
      i.times { alloc_obj }
      GC.start
    end
    AllocTrack.stop
  end
end
