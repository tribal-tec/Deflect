Changelog {#Changelog}
============

## Deflect 0.8

### 0.8.0 (17-09-2015)

* Fix warnings about QSocketNotifier
* Add DeflectQml library to create QML applications which render offscreen
  and stream and receive events via Deflect

## Deflect 0.7

### 0.7.2 (03-09-2015)
* Add 'About' dialog to learn current used version
* Fix bundle generation on OSX, broken after 0.6.1

### 0.7.1 (01-09-2015)
* Fixed rare crash or hang together with stream and interaction

### 0.7.0 (19-08-2015)
* Added EVT_TAP_AND_HOLD EventType

## Deflect 0.6

### 0.6.1 (13-07-2015)
* DesktopStreamer: Minor fixes for desktop interaction, only show checkbox on
  supported platforms (OSX)
* DesktopStreamer OSX: zip the application for Puppet deployment

### 0.6.0 (28-06-2015)
* Many classes of the Server part have been renamed
* Various API fixes
* Project-wide codestyle cleanup

## Deflect 0.5

### 0.5.1 (12-05-2015)
* Streamers can register for events immediately, without waiting for the
  DisplayCluster host to have opened a window
* Desktopstreamer supports autodiscovery of DisplayCluster hosts using Zeroconf
* Add interaction from the Wall with the DesktopStreamer host (Mac OS X
  only)

## Deflect 0.4

### 0.4.1 (24-04-2015)
* DesktopStreamer properly handles AppNap on OSX 10.9.
* DesktopStreamer detects Retina displays automatically (no retina checkbox).

### 0.4.0 (07-01-2015)
* First release, based on the DisplayCluster 0.4 dcStream library
