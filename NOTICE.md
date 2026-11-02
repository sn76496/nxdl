# Licensing and acknowledgments

NXDL is distributed under the Mozilla Public License, version 2.0 (MPL-2.0).
See [LICENSE](LICENSE). The C sources, public/internal headers, examples, tests,
build scripts, formatter settings, and accompanying project documentation are
provided under MPL-2.0. No Exhibit B incompatibility notice is applied to NXDL.
The text of Exhibit B reproduced in LICENSE is part of the license template,
not an invocation of that notice.

## CTRDL and CTRL

NXDL's design and API were developed with reference to
[CTRDL](https://github.com/kynex7510/CTRDL) by Kynex7510, an MPL-2.0 implementation
of the dl* APIs for Nintendo 3DS. The reviewed revision was
`2fb8fa1f40be5e6a340da59ec9f8f25b7b2f8a14`. NXDL retains its own Switch implementation
and adopts CTRDL-like source formatting. This acknowledgment does not transfer
ownership of CTRDL's work or imply endorsement by its author.

CTRDL uses [CTRL](https://github.com/kynex7510/CTRL) by Kynex7510, reviewed at
`626a356e96f263cb3d88052a6756afab2f61da60`. CTRL is distributed under the Boost
Software License 1.0. NXDL does not include CTRL source or link against CTRL;
its memory backend is implemented using libnx.

Other implementation references included libtransistor, libnx-ext/libnx-ld,
libnx-dlink, libnx, and Atmosphere's memory-management implementation. These are
references, not bundled source. Their licenses are not replaced by NXDL's
license. The installed devkitA64/libnx toolchain remains separately licensed.

## Distributing binaries and source

When publishing a compiled library or application containing NXDL, make the
corresponding NXDL source (including modifications and build files) available
under MPL-2.0 and tell recipients where to obtain it. Preserve license and
attribution notices. A GitHub source release matching each binary release is
one way to supply that source; include its actual URL with the binary release.
Do not ship a binary-only release with no source-availability notice.

The installation target installs LICENSE and this notice under
`share/licenses/nxdl/` in addition to the archive and headers. These files do not
replace the need to supply corresponding source when distributing binaries.

## Rockbox integration

MPL-2.0 section 3.3 permits additionally distributing covered code under an
applicable Secondary License when it is combined with work under that license,
provided the covered code is not marked Incompatible With Secondary Licenses.
GPL version 2 or later is included in the definition of Secondary License.
NXDL uses MPL-2.0 without the Exhibit B opt-out, preserving that route for a
Rockbox combination. This does not simply relicense upstream CTRDL as GPL-only
or settle the licenses of other codecs/dependencies used by an application.

References: [MPL-2.0 sections 1.12 and 3](https://www.mozilla.org/en-US/MPL/2.0/),
[Mozilla's GPL compatibility guidance](https://www.mozilla.org/en-US/MPL/2.0/FAQ/).
