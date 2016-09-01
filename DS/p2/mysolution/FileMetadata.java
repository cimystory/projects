/*
 * Object stores detials of each file
 * stored in FileVersionTable for files on cache
 */
import java.io.*;

	public class FileMetadata {
	        public long fileLength;
	        public int fileVersion;
	        public String fileName;
	        public int clientCount;
	        public int writerCount;
	        public int readerCount;
	        public long lastAccessTime;
	        public String nextVersionName;
	        
	        FileMetadata(String path, int version, long length, int noOfClients, int noOfWriters){
	        	this.fileName = path;
	        	this.fileLength = length;
	        	this.fileVersion = version;
	        	this.clientCount = noOfClients;
	        	this.readerCount = 0;
	        	this.writerCount = noOfWriters;
	        	this.lastAccessTime = System.currentTimeMillis();
	        	this.nextVersionName = null;
	        }
	}
