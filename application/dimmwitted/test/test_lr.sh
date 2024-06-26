
./dw-lr-train -s 10000 -e 100 -r 1000 ./test/a6a  
ACCURACY=`./dw-lr-test ./test/a6a.t ./test/a6a.model ./test/a6a.output | grep 'Testing acc' | sed 's/^.*=//g'`

if [ `echo "${ACCURACY} > 0.84" | bc` -eq 1 ]; then
	echo "GREAT, ${ACCURACY} > 0.84"
	exit 0
else
	echo ":(, ${ACCURACY} <= 0.84"
	exit 2
fi
