#!/bin/sh
if ! test -f ../prototxt/faceanglenet/faceanglenet_train_v2.prototxt ;then
	echo "error: ../prototxt/faceanglenet_train_v2.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
if ! test -f ../prototxt/faceanglenet/faceanglenet_test_v2.prototxt ;then
	echo "error: ../prototxt/faceanglenet_test_v2.prototxt does not exit."
	echo "please generate your own model prototxt primarily."
        exit 1
fi
../../../../build/tools/caffe train --solver=../solver/faceangle_solver_train_v2.prototxt -gpu 0 \
--snapshot=../snapshot/deepanoFaceangle_v2_iter_301318.solverstate
