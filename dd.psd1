@{
    schema = 1
    project = @{
        name = 'platform-h'
        type = 'library'
        'default-target' = 'lib'
    }
    dependencies = @{ owner = 'dd' }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'lib'
            kind = 'library'
            'cmake-target' = 'platform-core'
            'test-label' = 'platform'
            'debug-path' = 'build/debug/{libprefix}platform-core{lib}'
            'release-path' = 'build/release/{libprefix}platform-core{lib}'
            platforms = @('x64-windows')
        }
    )
}
