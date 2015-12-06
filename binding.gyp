{
    "targets": [
        {
            "target_name": "posixread",
            "sources": [ "src/cpp/posixread.cpp" ],
            "include_dirs" : [
                "<!(node -e \"require('nan')\")"
            ],
            "libraries": [ ]
        }
    ]
}
