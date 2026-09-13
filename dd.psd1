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
            'cmake-target' = 'platform'
            'test-label' = 'platform'
            'debug-path' = 'build/debug/{libprefix}platform{lib}'
            'release-path' = 'build/release/{libprefix}platform{lib}'
            platforms = @('x64-windows')
        }
    )
}
