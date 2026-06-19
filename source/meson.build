project(
  'nutrimatic', ['cpp'],
  version: '0.1',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'werror=true',
  ],
)

# Disable warnings that openfst headers trigger
if meson.get_compiler('cpp').get_id() == 'clang'
  # clang-only args
  extra_toolchain_args = ['-Wno-deprecated-copy']
else
  # gcc-only args
  extra_toolchain_args = [
    '-Wno-dangling-pointer',
    '-Wno-missing-template-keyword',
  ]
endif
project_args = [
  '-D_USE_MATH_DEFINES',
  '-Wno-ignored-qualifiers',
  '-Wno-sign-compare',
  '-Wno-overloaded-virtual',
  '-Wno-unused-parameter',
  '-Wno-vla',
] + extra_toolchain_args

add_project_arguments(
  project_args,
  language: ['cpp'],
)

fst_dep = dependency('openfst')
tre_dep = dependency('tre')
xml2_dep = dependency('libxml-2.0')
icu_uc_dep = dependency('icu-uc')
icu_i18n_dep = dependency('icu-i18n')
unicode_deps = [icu_uc_dep, icu_i18n_dep]

unicode_lib = static_library(
  'unicode',
  ['unicode.cpp'],
  dependencies: unicode_deps,
)
unicode_dep = declare_dependency(
  link_with: unicode_lib,
  dependencies: unicode_deps,
)

cli_utf8_lib = static_library(
  'cli-utf8',
  ['cli-utf8.cpp'],
  dependencies: unicode_dep,
)

index_lib = static_library(
  'index',
  ['index-format.cpp', 'index-reader.cpp', 'index-walker.cpp', 'index-writer.cpp'],
  dependencies: unicode_dep,
)

index_builder_lib = static_library(
  'index-builder',
  ['index-builder.cpp'],
  dependencies: unicode_dep,
)

search_lib = static_library(
  'search',
  ['search-driver.cpp', 'search-printer.cpp'],
  link_with: [index_lib],
)

expr_lib = static_library(
  'expr', [
    'expr-anagram.cpp',
    'expr-filter.cpp',
    'expr-intersect.cpp',
    'expr-optimize.cpp',
    'expr-parse.cpp'
  ],
  link_with: [search_lib],
  dependencies: [fst_dep, unicode_dep],
)

foreach p : ['remove-markup']
  executable(p, p + '.cpp', dependencies: [tre_dep, xml2_dep], install: true)
endforeach

foreach p : ['merge-indexes', 'dump-index', 'explore-index']
  executable(
    p,
    p + '.cpp',
    link_with: [index_lib, cli_utf8_lib],
    dependencies: unicode_dep,
    install: true,
  )
endforeach

executable(
  'make-index',
  'make-index.cpp',
  link_with: [index_lib, index_builder_lib, cli_utf8_lib],
  dependencies: unicode_dep,
  install: true,
)

foreach p : ['find-anagrams', 'find-phone-words']
  executable(
    p,
    p + '.cpp',
    link_with: [search_lib, cli_utf8_lib],
    dependencies: unicode_dep,
    install: true,
  )
endforeach

foreach p : ['find-expr', 'test-expr']
  links = [expr_lib, cli_utf8_lib]
  if p == 'test-expr'
    links += index_builder_lib
  endif
  executable(
    p,
    p + '.cpp',
    link_with: links,
    dependencies: [fst_dep, unicode_dep],
    install: true,
  )
endforeach

executable(
  'test-find-expr',
  'test-find-expr.cpp',
  link_with: [index_lib, index_builder_lib],
  dependencies: unicode_dep,
)

executable('test-unicode', 'test-unicode.cpp', dependencies: unicode_dep)
executable(
  'test-index-format',
  'test-index-format.cpp',
  link_with: index_lib,
  dependencies: unicode_dep,
)
executable(
  'test-index-builder',
  'test-index-builder.cpp',
  link_with: index_builder_lib,
  dependencies: unicode_dep,
)

executable(
	'test-rank-lines',
	['test-rank-lines.cpp', 'rank-lines-lib.cpp'],
	link_with: [expr_lib, index_builder_lib],
	dependencies: [fst_dep, unicode_dep],
)

executable(
  'rank-lines',
  ['rank-lines.cpp', 'rank-lines-lib.cpp'],
  link_with: expr_lib,
  dependencies: fst_dep,
  install: true,
)
