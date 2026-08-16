function(renderojn_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()

# Tells MSVC to read source files as UTF-8 rather than the system ANSI code page.
# Without this a BOM-less source with UTF-8 bytes above 0x7F is misread on a
# non-Western Windows -- a trailing DBCS lead byte in a comment can swallow the
# next line -- so every target that compiles our sources needs it, not just the
# ones that also want the full warning set.
function(renderojn_apply_source_charset target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
  endif()
endfunction()
