# The third-party notice text a distributed lain binary prints for --licenses.
#
# Some dependencies oblige a redistributor to say, prominently, that the work uses them and
# how to obtain their corresponding source — FFmpeg's LGPL is the first (ADR-0019). That is a
# runtime obligation: it has to reach whoever runs the binary, not just whoever reads the
# repository.
#
# A dependency that carries an obligation writes its notice to a file and appends the path to
# the LAIN_THIRD_PARTY_NOTICES global property; lain::app concatenates whatever was registered
# into one generated source. Membership is therefore build-discovered, exactly as the image
# codec aggregator's is: a dependency that is not built contributes nothing, so the text never
# claims something the binary does not contain.
#
# Deliberately NOT a checked-in NOTICE file. A transcription is a second copy of facts that
# already exist in the dependency's own manifest, and its failure mode is silent — it goes on
# claiming the old version after a bump, which is worse than useless for a licence claim. The
# README's inventory table stays the human-facing record (ADR-0015); this is the machine one,
# and it is derived.

# Generate `outVar`'s source file from every registered notice. Call after all dependencies
# have been configured.
function(lain_generate_notices outVar)
	get_property(_notices GLOBAL PROPERTY LAIN_THIRD_PARTY_NOTICES)

	set(LAIN_NOTICE_TEXT "")
	foreach(_notice IN LISTS _notices)
		file(READ "${_notice}" _text)
		string(APPEND LAIN_NOTICE_TEXT "${_text}\n")
	endforeach()

	# A raw string literal, so the notice text needs no escaping and stays readable in the
	# generated file. The delimiter is deliberately obscure: a notice containing the exact
	# sequence )lain_notice" would otherwise end the literal early.
	set(_out "${CMAKE_CURRENT_BINARY_DIR}/notices.cpp")
	configure_file("${CMAKE_SOURCE_DIR}/cmake/notices.cpp.in" "${_out}" @ONLY)
	set(${outVar} "${_out}" PARENT_SCOPE)
endfunction()
