home_iot:		user.c sensor.c manager.c processes.h
			gcc -g -Wall -pthread manager.c processes.h -o manager
			gcc -g -Wall -pthread sensor.c processes.h -o sensor
			gcc -g -Wall -pthread user.c processes.h -o user 


