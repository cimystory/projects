/*
 * CacheHandler - All the cache functions are handled
 * by this class
 * member freeSpace keeps track of available space in cache
 */
import java.io.File;
import java.io.FileNotFoundException;
import java.io.RandomAccessFile;
import java.util.Iterator;
import java.util.concurrent.ConcurrentHashMap;

public class CacheHandler {

	public static long rmi_window = 1024 * 1024;
	int maxSize;
	int freeSpace;
	static ConcurrentHashMap<String, FileMetadata> FileVersionTable = new ConcurrentHashMap<String, FileMetadata>();

	CacheHandler(int size) {
		this.maxSize = size;
		this.freeSpace = size;
	}

	/*
	 * evict - evicts cache until the space is more than the length parameter
	 * linear search through the hashmap to find the LRU file
	 */
	int evict(long length, String root) {
		System.err.println("Calling Eviction");
		FileMetadata victim = null;
		String target = "";
		while (this.freeSpace < length) {
			long min = Long.MAX_VALUE;
			if(FileVersionTable.isEmpty()){
				return -1;
			}
			for (String path : FileVersionTable.keySet()) {
				FileMetadata fm1 = FileVersionTable.get(path);
				if (fm1.lastAccessTime < min && fm1.readerCount == 0 && fm1.writerCount == 0) {
					min = fm1.lastAccessTime;
					victim = fm1;
					target = path;
				}
			}
			if(target == ""){
				return -1;
			}
			System.err.println("Victim to be evicted - " + victim.fileName);

			File tmp = new File(root.concat("/").concat(target));
			long len = tmp.length();
			FileVersionTable.remove(target);
			tmp.delete();
			this.freeSpace += len;
		}
		System.err.println("Eviction success");
		return 0;
	}

	/*
	 * addMetadata - Add fileMetadata to FileVersionTable
	 * update the object if the entry already present in table
	 */
	void addMetadata(String path, int version, long length) {
		int noOfClients = 0;
		int noOfWriters = 0;
		if (FileVersionTable.containsKey(path)) {
			FileMetadata fm = FileVersionTable.get(path);
			noOfClients = fm.clientCount;
			noOfWriters = fm.writerCount;
		}
		FileVersionTable.put(path, new FileMetadata(path, version, length, noOfClients, noOfWriters));
	}

	/*
	 * update the original file copy with details of
	 * writer's copy
	 */
	void updateParentMetadata(String path) {
		if (FileVersionTable.containsKey(path)) {
			FileMetadata fm = FileVersionTable.get(path);
			int noOfClients = fm.clientCount;
			int noOfWriters = fm.writerCount;
			fm.clientCount = noOfClients + 1;
			fm.writerCount = noOfWriters + 1;
			fm.lastAccessTime = System.currentTimeMillis();
			FileVersionTable.remove(path);
			FileVersionTable.put(path, fm);
		}
	}

	void addReader(String path) {
		FileMetadata fm = FileVersionTable.get(path);
		int noOfClients = fm.clientCount + 1;
		int noOfReaders = fm.readerCount + 1;
		fm.lastAccessTime = System.currentTimeMillis();
		fm.clientCount = noOfClients;
		fm.readerCount = noOfReaders;
		FileVersionTable.put(path, fm);
	}

	void removeReader(String path) {
		FileMetadata fm = FileVersionTable.get(path);
		int noOfReaders = fm.readerCount - 1;
		if(noOfReaders < 0){
			noOfReaders = 0;
		}
		fm.readerCount = noOfReaders;
		fm.lastAccessTime = System.currentTimeMillis();
		FileVersionTable.put(path, fm);
	}

	/*
	 * update version of writer's copy on 
	 * cache
	 */
	void updateVersion(String path) {
		FileMetadata fm = FileVersionTable.get(path);
		int version = fm.fileVersion + 1;
		fm.fileVersion = version;
		FileVersionTable.put(path, fm);
	}

	void removeWriter(String path) {
		FileMetadata fm = FileVersionTable.get(path);
		int noOfWriters = fm.writerCount - 1;
		fm.writerCount = noOfWriters;
		fm.lastAccessTime = System.currentTimeMillis();
		FileVersionTable.put(path, fm);
	}

	void deleteFile(String path) {
		FileVersionTable.remove(path);
	}

	/*
	 * update the original master copy with the name of latest
	 * version of the file present on cache
	 */
	void updateParentLatestVersion(String parentFile, String newFile) {
		FileMetadata fm = FileVersionTable.get(parentFile);
		fm.nextVersionName = newFile;
		FileVersionTable.put(parentFile, fm);
	}

	String cloneFile(String path, String root) {
		FileMetadata fm = FileVersionTable.get(path);
		int numberOfWriters = fm.writerCount + 1;
		int numberofClients = fm.clientCount + 1;
		fm.writerCount = numberOfWriters;
		fm.clientCount = numberofClients;
		fm.lastAccessTime = System.currentTimeMillis();
		FileVersionTable.put(path, fm);
		if(fm.fileLength > this.freeSpace){
			int retvalue = this.evict(fm.fileLength, root);
			if(retvalue < 0 ){ //eviction failed
				return null;
			}
		}
		// clone new file
		try {
			RandomAccessFile srcFile = new RandomAccessFile(root.concat("/").concat(path), "rw");
			String newFileName = path.concat(Integer.toString(numberofClients));
			// set name of cloned copy appended with number of clients 
			RandomAccessFile dstFile = new RandomAccessFile(root.concat("/").concat(newFileName), "rw");
			dstFile.seek(0);
			byte[] buff;
			long offset = 0;
			long length = srcFile.length();
			while (offset < length) {
				long len = rmi_window < (length - offset) ? rmi_window : (length - offset);
				buff = new byte[(int) len];
				srcFile.seek(offset);
				srcFile.read(buff);
				dstFile.write(buff, 0, (int) len);
				offset += len;
				dstFile.seek(offset);
			}
			FileVersionTable.put(newFileName, new FileMetadata(path, 0, length, 1, 1));
			this.freeSpace -= length;
			srcFile.close();
			dstFile.close();
			return newFileName;
		} catch (FileNotFoundException e) {
			return null;
		} catch (Exception e) {
			return null;
		}
	}
}
