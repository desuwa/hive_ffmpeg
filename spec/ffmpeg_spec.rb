#!/usr/bin/env ruby

require 'minitest/autorun'
require 'fileutils'
require 'hive_ffmpeg'

include Hive

class FFmpegSpec < MiniTest::Spec

  ROOT = File.expand_path(File.dirname(__FILE__))
  TEST_FILE = "#{ROOT}/test.webm"
  TMP_DIR = "#{ROOT}/tmp"

  def cleanup_tmp_dir
    FileUtils.rm_r(TMP_DIR) if File.directory?(TMP_DIR)
    FileUtils.mkdir(TMP_DIR)
  end

  def setup
    @@ff = FFmpeg.new(TEST_FILE)
  end

  def teardown
    @@ff.close
  end

  describe FFmpeg do
    it 'can be instantiated' do
      begin
        ff = FFmpeg.new(TEST_FILE)
      ensure
        ff.must_be_instance_of(FFmpeg)
        ff.close if ff
      end
    end

    it 'can accept a block' do
      ff = FFmpeg.open(TEST_FILE) { |ff| ff }
      ff.must_be_instance_of(FFmpeg)
    end

    it 'returns the number of streams' do
      @@ff.nb_streams.must_equal 1
    end

    it 'returns stream information' do
      streams = @@ff.streams
      streams.must_be_instance_of(Array)

      video = streams[0]
      video.must_be_instance_of(Hash)

      assert [
        :video, :audio, :data, :subtitle, :attachment, nil
      ].include?(video[:type])
      assert_equal :video, video[:type]
      assert_equal 'vp8', video[:codec]
      assert_equal 128, video[:width]
      assert_equal 128, video[:height]
      assert_equal Rational(1, 1), video[:sar]
    end

    it 'returns the duration in seconds' do
      assert_instance_of(Float, @@ff.duration)
    end

    it 'returns the container format' do
      assert_equal 'matroska,webm', @@ff.format
    end

    it 'returns the path of the input file' do
      assert_equal TEST_FILE, @@ff.path
    end

    describe '#close, #closed?' do
      it 'closes the input file and frees the format context' do
        @@ff.close
        assert_equal true, @@ff.closed?
        -> { @@ff.nb_streams }.must_raise(IOError)
      end

      it "is called automatically at the end of ::open's block" do
        ff = FFmpeg.open(TEST_FILE) do |ff|
          assert_equal false, ff.closed?
          ff
        end
        assert_equal true, ff.closed?
      end
    end

    describe '#save_frame' do
      it 'saves a frame and returns the resulting image dimensions' do
        cleanup_tmp_dir
        width, height = @@ff.save_frame("#{TMP_DIR}/frame.jpg")
        assert width == 128 && height == 128
        assert File.exist?("#{TMP_DIR}/frame.jpg")
      end

      it 'accepts a format parameter' do
        cleanup_tmp_dir
        @@ff.save_frame("#{TMP_DIR}/frame.nope", format: :jpg)
        assert File.exist?("#{TMP_DIR}/frame.nope")
      end

      it 'accepts a Fixnum offset parameter as percentage of the duration' do
        cleanup_tmp_dir
        @@ff.save_frame("#{TMP_DIR}/frame.jpg", offset: 50)
        assert File.exist?("#{TMP_DIR}/frame.jpg")
      end

      it 'accepts a max_size parameter for downscaling' do
        cleanup_tmp_dir
        @@ff.save_frame("#{TMP_DIR}/frame.jpg", max_size: 64)
        assert File.exist?("#{TMP_DIR}/frame.jpg")
      end

      it 'accepts a JPEG quality parameter as an integer between 0 and 100' do
        cleanup_tmp_dir
        @@ff.save_frame("#{TMP_DIR}/frame.jpg", quality: 50)
        assert File.exist?("#{TMP_DIR}/frame.jpg")
      end
    end
 end

 Minitest.after_run do
    FileUtils.rm_r(TMP_DIR) if File.directory?(TMP_DIR)
 end

end
