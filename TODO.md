# Implementation Plan for Audio Streaming and FFT Improvements

- Improve TUI (add colours)
- Add TUI Visualization coloured animation flow pixel effect
- On frontend - dim the "colours" of the theme when 'paused' and them bring them back to saturation on play (intensity should be based on volume level) - Add additional filter effects (like colours, gradients, etc) on frontend app.
- Make the style more linecore aesthetic on the frontend controls
- Improve metadata - Have a small info tooltip with additional information on artist, track, album, if present.
- Allowing for stream-relaying to allow for services like ice cast streams to be relayed through (such as from playlists) as well as allowing for local streaming to be passed through (such as from OBS stream or local ice cast)
- Fix Websocket Streaming issue for FFT - Could be issue with Https on local??? 
- Add "Admin" mode (Remote Web Controller with Permissions) and remote control API (only for admin)
- Improve "Coder" feature - it should allow for sequencing, channel / tracks, and fix up issues with record, loop - should be more like existing code to music systems.
