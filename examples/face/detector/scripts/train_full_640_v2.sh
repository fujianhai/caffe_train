#!/bin/sh
if ! test -f ../prototxt/Full_640x640/train_v2.prototxt ;then
	echo "error: ../../prototxt/Full_640x640/train_v2.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
if ! test -f ../prototxt/Full_640x640/test_v2.prototxt ;then
	echo "error: ../../prototxt/Full_640x640/test_v2.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
../../../../build/tools/caffe train --solver=../prototxt/Full_640x640/solver_v2.prototxt -gpu 1 \
#--snapshot=../snapshot/face_v5_iter_5000.solverstate
