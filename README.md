# RenderOJN #
Renders O2Jam OJN/OJM to MP3/WAV/OGG music file

- **Author**: Yana Nugraha
- **Author Homepage**: http://djzmo.com
- **Latest Version**: 0.8.2

**IMPORTANT:**
This project is no longer maintained. Use at your own risk.

--------------------------------------------------------------------------------------------------

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

## Summary ##

RenderOJN is a utility which is able to render an OJN file into PCM and encode to
preferred audio file format.

See it in action: https://www.youtube.com/watch?v=snYnd_IvmbM

## Usage Notes ##

- In realtime mode, it is HIGHLY recommended to do nothing with your computer
  while rendering a keysounded music to preserve output quality.
- In quick mode, output quality might not be satisfying on keysounded music.

## Version History ##

### v0.8.2
Released: June 2012
- Able to encode to OGG with automatic tagging.
- Added output quality option.
- Added "Genre" field to the music tag.
- Added application icon.
- Added progress percentage on time-consuming processes.
- Fixed chopped music ending bug.

### v0.8.0
- Able to encode to MP3 with automatic ID3 tagging.
- Keysounded notes might or might not be rendered properly in quick mode.

## Usage ##

Usage: ```RenderOJN [inputfile [options]]```

Rendering Options:

```
  --rendermode <mode>       Rendering Mode (quick, realtime). Default: quick
  --format <format>         Output Format (wav, mp3, ogg). Default: mp3
  --outfile <filename>      Output Filename. Default: <inputfile>.<format>
                            Whitespace is not allowed.
  --quality <quality>       Output Quality (for mp3 and ogg). Default: 3
                            3 - Best, 2 - Standard, 1 - Poor
```

Misc. Options:

```
  --difficulty <difficulty> Note Difficulty (e, n, h). Default: h
  --play                    Play the music instead of generating an output file
  --help                    Display this text
```

Example:

```RenderOJN o2ma100.ojn --outfile BachAlive.mp3 --quality 2

RenderOJN o2ma100.ojn --rendermode realtime --format wav

RenderOJN o2ma100.ojn --play

RenderOJN --help```

## Dependencies ##

- [FMOD Ex Programmer's API version 4](http://fmod.org)
- [libsndfile](https://github.com/erikd/libsndfile)
- [taglib](https://github.com/taglib/taglib)
- [Boost 1.40+](http://boost.org) for Boost Date Time and Program Options

### License ###

This is an open-sourced software licensed under the [GNU GPL v3 license](http://www.gnu.org/licenses/gpl-3.0.en.html).