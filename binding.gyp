{
    "targets": [
        {
            "target_name": "posix-read",
            "sources": [ "src/cpp/posix-read.cpp" ],
            "include_dirs" : [
                "<!(node -e \"require('nan')\")"
            ],
            "libraries": [ ]
        }
    ]
}
