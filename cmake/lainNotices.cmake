# The licence notices a distributed lain binary prints for --licenses: lain's own, its submodules',
# and every third-party dependency's.
#
# Every dependency compiled or linked into a lain::app binary registers a notice, whether or not its
# licence strictly asks for one in a binary (ADR-0015, amended 2026-10-23). MIT, BSD-3-Clause and
# libtiff's licence ask that the copyright and permission notice accompany copies; FFmpeg's LGPL asks
# for a prominent notice and the corresponding source (ADR-0019). Either is a runtime obligation: it
# has to reach whoever runs the binary, not just whoever reads the repository. One rule for all of
# them keeps the legal reading of each licence out of the build.
#
# A dependency writes its notice to a file and appends the path to the LAIN_THIRD_PARTY_NOTICES
# global property, through lain_register_notice() below or, for the three whose notice is more than a
# licence (FFmpeg, OpenCV, Eigen), by hand; lain::app concatenates whatever was registered into one
# generated source. Membership is therefore build-discovered, exactly as the image codec aggregator's
# is: a dependency that is not built contributes nothing, so the text never claims something the
# binary does not contain.
#
# Deliberately NOT a checked-in NOTICE file. A transcription is a second copy of facts that
# already exist in the dependency's own manifest, and its failure mode is silent — it goes on
# claiming the old version after a bump, which is worse than useless for a licence claim. The
# README's inventory table stays the human-facing record (ADR-0015); this is the machine one,
# and it is derived.

include_guard(GLOBAL)

# A fetched dependency passes an empty PACKAGE_DIR / PACKAGE_VERSION, and an empty one is meant. NEW
# reads it as empty instead of warning (CMake 3.31); a function keeps the policies of its definition.
if(POLICY CMP0174)
	cmake_policy(SET CMP0174 NEW)
endif()

# Register one dependency's notice: a header line naming it, its version and its licence, then its
# own licence files VERBATIM, read from the fetched source now and indented to sit under the header.
#
#   lain_register_notice(<id>
#       NAME <name> VERSION <pinned version> SPDX <licence expression>
#       SOURCE_DIR <fetched source> FILES <licence file, relative to it>...
#       [PACKAGE_DIR <where find_package found it> PACKAGE_VERSION <the version it found>]
#       [DETAILS <line>...]
#       [STAGE])
#
# An empty SOURCE_DIR means a system package satisfied the dependency, so nothing was fetched and
# there is no file here to read. The notice then names the package and where it is, whose own licence
# files came with it, rather than a text this build never saw. STAGE copies the files into
# third-party/<id>/ beside the binaries and points at them instead of printing them, as OpenCV's
# notice does, for a licence too long to print (Apache-2.0's two hundred lines).
#
# A named file missing from the source FAILS configure: a pin bump that moves a licence file must not
# quietly drop it from the notice.
function(lain_register_notice id)
	cmake_parse_arguments(PARSE_ARGV 1 _n "STAGE" "NAME;VERSION;SPDX;SOURCE_DIR;PACKAGE_DIR;PACKAGE_VERSION" "FILES;DETAILS")

	if(_n_SOURCE_DIR)
		set(_version "${_n_VERSION}")
	elseif(_n_PACKAGE_DIR)
		set(_version "${_n_PACKAGE_VERSION}")
	else()
		message(FATAL_ERROR "lain_register_notice(${id}): no SOURCE_DIR and no PACKAGE_DIR, so nothing says where its licence is")
	endif()
	string(STRIP "${_n_NAME} ${_version}" _named) # a package need not report a version
	set(_text "${_named} — ${_n_SPDX}\n")
	if(NOT _n_SOURCE_DIR)
		string(APPEND _text "  From the system package at ${_n_PACKAGE_DIR}, whose own licence files carry its notice.\n")
	endif()
	foreach(_line IN LISTS _n_DETAILS)
		string(APPEND _text "  ${_line}\n")
	endforeach()

	if(_n_SOURCE_DIR)
		set(_staged "${CMAKE_BINARY_DIR}/third-party/${id}")
		foreach(_file IN LISTS _n_FILES)
			if(NOT EXISTS "${_n_SOURCE_DIR}/${_file}")
				message(FATAL_ERROR "lain_register_notice(${id}): ${_n_SOURCE_DIR}/${_file} does not exist")
			endif()
			if(_n_STAGE)
				get_filename_component(_name "${_file}" NAME)
				file(MAKE_DIRECTORY "${_staged}")
				configure_file("${_n_SOURCE_DIR}/${_file}" "${_staged}/${_name}" COPYONLY)
				continue()
			endif()
			# Verbatim but for layout: line endings made \n, leading and trailing blank lines dropped,
			# and every non-empty line indented two spaces, as the other notices' bodies are.
			file(READ "${_n_SOURCE_DIR}/${_file}" _licence)
			string(REPLACE "\r\n" "\n" _licence "${_licence}")
			string(REGEX REPLACE "^\n+" "" _licence "${_licence}")
			string(REGEX REPLACE "\n+$" "" _licence "${_licence}")
			string(REGEX REPLACE "\n([^\n])" "\n  \\1" _licence "${_licence}")
			string(APPEND _text "\n  ${_licence}\n")
		endforeach()
		if(_n_STAGE)
			string(APPEND _text "  Full licence text: third-party/${id}/\n")
		endif()
	endif()

	set(_notice "${CMAKE_BINARY_DIR}/notices/${id}.txt")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/notices")
	file(WRITE "${_notice}" "${_text}")
	set_property(GLOBAL APPEND PROPERTY LAIN_THIRD_PARTY_NOTICES "${_notice}")
endfunction()

# Generate `outVar`'s source file from every registered notice. Call after all dependencies
# have been configured.
function(lain_generate_notices outVar)
	get_property(_notices GLOBAL PROPERTY LAIN_THIRD_PARTY_NOTICES)

	# Raw string literals, so the notice text needs no escaping and stays readable in the generated
	# file. The delimiter is deliberately obscure: a notice containing the exact sequence
	# )lain_notice" would otherwise end the literal early.
	#
	# Several literals, not one: MSVC refuses a string literal over 16380 bytes (C2026), and the
	# notices together are several times that. Each piece is at most 8000 bytes and cut only at a
	# line end, since a cut anywhere else could split a UTF-8 character between two literals.
	set(LAIN_NOTICE_PIECES "")
	foreach(_notice IN LISTS _notices)
		file(READ "${_notice}" _rest)
		string(APPEND _rest "\n")
		while(NOT _rest STREQUAL "")
			string(LENGTH "${_rest}" _length)
			set(_cut ${_length})
			if(_length GREATER 8000)
				string(SUBSTRING "${_rest}" 0 8000 _head)
				string(FIND "${_head}" "\n" _cut REVERSE)
				if(_cut EQUAL -1)
					message(FATAL_ERROR "${_notice} has a line longer than 8000 bytes, which no piece can hold")
				endif()
				math(EXPR _cut "${_cut} + 1")
			endif()
			string(SUBSTRING "${_rest}" 0 ${_cut} _piece)
			string(SUBSTRING "${_rest}" ${_cut} -1 _rest)
			string(APPEND LAIN_NOTICE_PIECES "\t\t\tR\"lain_notice(${_piece})lain_notice\",\n")
		endwhile()
	endforeach()

	# The template beside this file rather than under CMAKE_SOURCE_DIR, which is a consumer's root
	# when lain is bundled.
	set(_out "${CMAKE_CURRENT_BINARY_DIR}/notices.cpp")
	configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/notices.cpp.in" "${_out}" @ONLY)
	set(${outVar} "${_out}" PARENT_SCOPE)
endfunction()
