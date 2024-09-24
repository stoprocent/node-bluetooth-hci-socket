{
  'variables': {
    'openssl_fips' : '' 
  },
  "targets": [
    {
      "target_name": "bluetooth_hci_socket",
      'defines': [
        'NAPI_CPP_EXCEPTIONS=1'
      ],
      'sources': [],
      'include_dirs': [
        "<!@(node -p \"require('node-addon-api').include\")",
         "include"
      ],
      'dependencies': [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      'cflags!': [ '-fno-exceptions', '-std=c99' ],
      'cflags_cc!': [ '-fno-exceptions', '-std=c++20' ],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++20',
        'MACOSX_DEPLOYMENT_TARGET': '12'
      },
      'conditions': [
        ['OS=="linux" or OS=="android" or OS=="freebsd"', {
          "sources": [ 
            "<!@(node -p \"require('fs').readdirSync('src').map(f=>'src/'+f).join(' ')\")"
          ]
        }],
        ['OS=="win"', {
          'defines': [
            '_Static_assert=static_assert'
          ],
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [ '-std:c++20', ],
              'ExceptionHandling': 1
            }
          }
        }, { # OS != "win",
          'defines': [
            'restrict=__restrict'
          ],
        }]
      ],
    }
  ]
}