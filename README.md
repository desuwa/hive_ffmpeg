# muh webm thumbnails

```ruby
require 'hive_ffmpeg'

ff = Hive::FFmpeg.open('video.webm') do |ff|
  ff.format
  # matroska,webm
  ff.duration
  # 0.979
  ff.nb_streams
  # 1
  ff.streams
  # [ { :type=>:video, :codec=>"vp8", :width=>1280, :height=>720, :sar=>(1/1) } ]
  ff.save_frame('frame.jpg', quality: 75, max_size: 250, offset: 50)
  # [ 250, 140 ]
  ff
end

ff.closed?
# true
ff.path
# video.webm
```
