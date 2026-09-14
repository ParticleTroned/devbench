import("fork_version", { rootdir = path.join(path.directory(os.scriptdir()), "scripts") })

local function git(repository, arguments)
    local command = { "-C", repository }
    for _, argument in ipairs(arguments) do
        table.insert(command, argument)
    end
    return os.iorunv("git", command)
end

local function fails(callback)
    local failed = false
    try({
        callback,
        catch({
            function()
                failed = true
            end,
        }),
    })
    return failed
end

function main()
    local repository = path.absolute(os.tmpfile() .. "-fork-version")
    assert(path.directory(repository) == path.absolute(os.tmpdir()))
    os.mkdir(repository)

    try({
        function()
            git(repository, { "init", "-q" })
            io.writefile(path.join(repository, "fixture.txt"), "first\n")
            git(repository, { "add", "fixture.txt" })
            git(
                repository,
                { "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "first" }
            )

            assert(fails(function()
                fork_version(repository)
            end))
            git(repository, { "tag", "pt-v1.16.1" })
            local release, release_suffix = fork_version(repository)
            assert(release == "1.16.1" and release_suffix == "")

            io.writefile(path.join(repository, "fixture.txt"), "second\n")
            git(repository, { "add", "fixture.txt" })
            git(
                repository,
                { "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "second" }
            )
            local development, development_suffix = fork_version(repository)
            assert(development == "1.16.1")
            assert(development_suffix:match("^%.dev%.1%.[0-9a-f]+$"))

            git(repository, { "tag", "pt-v1.16.2-beta" })
            assert(fails(function()
                fork_version(repository)
            end))
        end,
        finally({
            function()
                os.rm(repository)
            end,
        }),
    })
end
