# main target, link threadpool, mapreduce, and distwc into distwc_program
wordcount: threadpool.o mapreduce.o distwc.o
	gcc -Wall -pthread -o wordcount threadpool.o mapreduce.o distwc.o

# Compile into object files
threadpool.o: threadpool.c
	gcc -Wall -c threadpool.c -o threadpool.o

mapreduce.o: mapreduce.c
	gcc -Wall -c mapreduce.c -o mapreduce.o

distwc.o: distwc.c
	gcc -Wall -c distwc.c -o distwc.o

# clean up
clean:
	rm -rf threadpool.o mapreduce.o distwc.o wordcount

# EXTRA BELOW
clean_result:
	rm -rf result-*

# run the 20 test files
run: wordcount
	./wordcount sample_inputs/sample1.txt sample_inputs/sample2.txt sample_inputs/sample3.txt sample_inputs/sample4.txt sample_inputs/sample5.txt sample_inputs/sample6.txt sample_inputs/sample7.txt sample_inputs/sample8.txt sample_inputs/sample9.txt sample_inputs/sample10.txt sample_inputs/sample11.txt sample_inputs/sample12.txt sample_inputs/sample13.txt sample_inputs/sample14.txt sample_inputs/sample15.txt sample_inputs/sample16.txt sample_inputs/sample17.txt sample_inputs/sample18.txt sample_inputs/sample19.txt sample_inputs/sample20.txt