for folder in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19; do
	qsub submit.pbs
	sleep 600
	mv erros run_$folder/.; sed '/^At/d' saida > run_$folder/saida; rm saida
done
