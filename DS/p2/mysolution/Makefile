all: Proxy.class Server.class FileDetails.class CacheHandler.class FileMetadata.class

%.class: %.java
	javac $<

clean:
	rm -f *.class
