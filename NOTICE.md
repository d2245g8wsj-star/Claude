# Third party notices

## JUCE

Crest is built with the [JUCE](https://juce.com) framework, which is **not**
covered by this repository's licence and is **not** redistributed here. JUCE is
dual licensed: AGPLv3, or a paid commercial licence.

This matters before you ship a binary:

* Releasing Crest as a closed source product requires a commercial JUCE licence.
* Releasing it under the AGPLv3 requires the complete corresponding source of
  the plugin to be offered to its users under the same terms.

Check the terms that apply to the JUCE version you build against, at
<https://juce.com/legal/juce-8-licence/>.

## VST

VST is a trademark of Steinberg Media Technologies GmbH. Building and
distributing a VST3 plugin requires agreeing to the Steinberg VST3 licence,
which ships with the JUCE VST3 headers. Register as a VST developer at
<https://developer.steinberg.help/> before releasing a VST3 binary.

## Audio Unit

Audio Unit is a trademark of Apple Inc. The AU target is built only on macOS.
