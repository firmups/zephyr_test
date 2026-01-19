macro(warn_all target)
  target_compile_options(
    ${target}
    PRIVATE -Wall # Most useful warnings
            -Wextra # Extra warnings
            -Wunused-macros # Unused Makros, in c-files only
            -Wold-style-definition # functions without parameter need `(void)`
            -Wswitch-enum # All cases handled, switching an enum
            -Wduplicated-cond # duplicated conditions in if-else-chain
            -Wlogical-op # for example and-ing identical expressions
            -Wformat=2 # stricter `printf`-checks
            -Wformat-truncation=2 # stricter `sprintf`-checks
            -Wformat-overflow=2 # stricter `snprintf`-checks
            -Warith-conversion # target type too small due to implicit
                               # conversion
            -Wnull-dereference # only works if optimizations are enabled
            -Wstrict-overflow # signed integer overflow
            # TODO(clag) to noisy -Wunused-const-variable=2 # Even if static
            # const appear in header
            -Wshadow # Avoid same name as outer scope
            -Wmissing-prototypes # Ensure all functions have a prototype
            -Wmissing-declarations # Ensure all functions have a declaration
            -Wwrite-strings # string literals as `const`
  )
endmacro()