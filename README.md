## Ruby bindings to FFmpeg

Mostly for video thumbnailing and stream info extraction.

### Installation

Recent FFmpeg/libav is required.
`libjpeg` and `libpng` are optional and only needed for frame extraction.

Debian/Ubuntu:
```
sudo apt-get install libavformat-dev libswscale-dev libjpeg-dev
```
Recent versions of Ubuntu have `libavformat-ffmpeg-dev` and `libswscale-ffmpeg-dev`

On other systems, installing `ffmpeg` and `libjpeg` packages should be enough.

Clone the repo, build the gem and install it:

```
gem build hive_ffmpeg.gemspec
gem install hive_ffmpeg-x.y.z.gem
```
You might need to provide the path to FFmpeg libs and headers on FreeBSD:
```
gem install hive_ffmpeg-x.y.z.gem -- --with-ffmpeg-dir=/usr/local
```
To build with libpng: `--with-png`  
To build without libjpeg: `--without-jpeg`

### Usage

```ruby
require 'hive_ffmpeg'

ff = Hive::FFmpeg.open('video.webm') do |ff|
  puts ff.format
  # matroska,webm
  puts ff.duration
  # 0.979
  puts ff.nb_streams
  # 1
  puts ff.streams
  # [ { :type=>:video, :codec=>"vp8", :width=>1280, :height=>720, :sar=>(1/1) } ]
  puts ff.save_frame('frame.jpg', quality: 75, max_size: 250, offset: 50)
  # [ 250, 140 ]
  ff
end

puts ff.closed?
# true
puts ff.path
# video.webm
```
See `spec/ffmpeg_spec.rb` for a slightly more thorough description.