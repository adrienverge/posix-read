{
    "targets": [
        {
            "target_name": "posix-read",
            "sources": [ "src/cpp/posix-read.cpp", "src/cpp/module.cpp" ],
            "include_dirs" : [
                "<!(node -e \"require('nan')\")"
            ],
            "libraries": [ ]
        }
    ]
}
