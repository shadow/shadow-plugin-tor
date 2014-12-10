#! /bin/bash

# run from inside a shadowtor conf dir to convert config to format used in 1.10.0

rm *dl *dat
python tgen_generate_configs.py
python tgen_replace_filetransfer.py shadow.config.xml shadow.config.xml
python tor_convert_args.py shadow.config.xml shadow.config.xml
python tor_create_v3bw.py

