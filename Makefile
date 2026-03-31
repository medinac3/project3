all:
	gcc -o main1 main1.c
	gcc -o main2 main2.c
	gcc -o main3 main3.c

clean:
	rm main1
	rm main2
	rm main3
