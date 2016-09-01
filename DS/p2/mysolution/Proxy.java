/* Proxy - Recieves requests from the client 
 * Maintains an on-demand cache that fetches files
 * from the server
 * Implements LRU eviction policy for the cache
 *  */

import java.io.*;
import java.util.*;
import java.nio.file.*;
import java.rmi.Naming;
import java.rmi.NotBoundException;
import java.util.concurrent.ConcurrentHashMap;

class Proxy {
	/* Global variables */
	public static ServerService srv = null;
	public static String newPath;
	public static String root;
	public static CacheHandler cache;
	public static ConcurrentHashMap<String, FileMetadata> FileVersionTable;
	//Locking object used in synchronization
	private static Object cachelock = new Object();
	// chunck size for file transfer
	public static long RMIWINDOW = 1024 * 1024;

	/* downloadFile - Downloads file from the server
	 * and updates the FileVersionTable
	 * evicts cache if necessary
	 */
	public static int downloadFile(String path, String cache_path, long len, int version) {
		int retValue = 0;
		// if file length greater than free space
		if (len > cache.freeSpace) {
			retValue = cache.evict(len, root);
			if (retValue < 0) { //No space in cache
				return retValue;
			}
		}
		try {
			RandomAccessFile tmpFile = new RandomAccessFile(root.concat("/").concat(cache_path), "rw");
			tmpFile.seek(0);
			byte[] buff;
			long off = 0;
			while (off < len) {
				long len1 = RMIWINDOW < (len - off) ? RMIWINDOW : (len - off);
				buff = new byte[(int) len1];
				buff = srv.getFile(path, off);
				tmpFile.write(buff, 0, (int) len1);
				off += len1;
				tmpFile.seek(off);
			}
			cache.freeSpace -= tmpFile.length();
			tmpFile.close();
			cache.addMetadata(cache_path, version, len);
			return 0;
		} catch (FileNotFoundException e) {
			System.err.println("Download file - File not found");
		} catch (Exception e) {
			System.err.println("Proxy - srv-getFile error" + e);
		}
		return 0;
	}

	/* 
	 * uploadFile - uploads file to server
	 * in chucks of RMIwindow size
	 */
	public static void uploadFile(String path, String cacheCopy) {
		try {
			RandomAccessFile tmpFile = new RandomAccessFile(root.concat("/").concat(cacheCopy), "rw");
			tmpFile.seek(0);
			byte[] buff;
			long off = 0;
			long fileLength = tmpFile.length();
			while (off < fileLength) {
				long len1 = RMIWINDOW < (fileLength - off) ? RMIWINDOW : (fileLength - off);
				buff = new byte[(int) len1];
				tmpFile.seek(off);
				tmpFile.read(buff);
				srv.uploadFile(path, buff, off, len1);
				off += len1;
			}
			tmpFile.close();
			System.err.println("Proxy - Called send file to server");
		} catch (Exception e) {
			System.err.println("Proxy - srv-sendFile error");
		}
	}

	/*
	 *  RMI to create file on server
	 */
	public static void createFile(String path) {
		try {
			srv.createFile(path);
			cache.addMetadata(path, 1, 0);
		} catch (Exception e) {
			System.err.println("Proxy error in create file");
		}
	}

	/*
	 * deleteFile - deletes file server
	 */
	public static int deleteFile(String path) {
		try {
			return srv.deleteFile(path);
		} catch (Exception e) {
			System.err.println("Proxy error in delete file!!");
			return -3;
		}
	}

	private static class FileHandler implements FileHandling {

		/*
		 * FileObject - clinet specific details of each file
		 */
		class FileObject {
			RandomAccessFile file;
			int option;
			String path;
			FileObject(RandomAccessFile _file, int o, String _path) {
				file = _file;
				option = o;
				path = _path;
			}
		}
		// fd table maintaid for each client
		Map<Integer, FileObject> fdTable = new HashMap<Integer, FileObject>();
		// assign FDs staring from 3
		int fd = 3;
		RandomAccessFile file;
		File tmpFile;
		File f;

		/*
		 * open - assigns file from cache to client
		 * If needed download file from server
		 * Make writers copy, maintain FileVersionTable
		 */
		public int open(String path, OpenOption o) {
			System.err.println("In open " + root.concat("/").concat(path) + " " + o);
			try {
				//check if file exists on server or is directory
				FileDetails fileDetails = srv.checkFile(path);
				long len = fileDetails.file_status;
				int version = fileDetails.file_version;
				int current_version = 0;
				int noOfReaders = 0;
				int noOfClients = 0;
				// get details of file on cache
				if (FileVersionTable.containsKey(path)) {
					FileMetadata fm = FileVersionTable.get(path);
					current_version = fm.fileVersion;
					noOfReaders = fm.readerCount;
					noOfClients = fm.clientCount;
				}
				switch (o) {
				case CREATE:
					if (len == -2) {
						return Errors.EISDIR;
					}
					if (len == -1) {
						synchronized (cachelock) {
							createFile(path);
							newPath = cache.cloneFile(path, root);
						}
					} else {
						synchronized (cachelock) {
							f = new File(root.concat("/").concat(path));
							if (!f.exists() || current_version != version) {
								// free space as file will be replaced
								if (f.exists()) {
									cache.freeSpace += f.length();
								}
								//already readers on original file, download a writers copy
								if (noOfReaders > 0) {
									int retvalue = downloadFile(path, path.concat(Integer.toString(noOfClients))
											, len, version);
									if (retvalue < 0) {
										return Errors.ENOMEM;
									}
									cache.updateParentMetadata(path);
									newPath = path.concat(Integer.toString(noOfClients));
								} else {
									// Call to the server to fetch file
									int retvalue = downloadFile(path, path, len, version);
									if (retvalue < 0) {
										return Errors.ENOMEM;
									}
									newPath = cache.cloneFile(path, root);
								}
							} else {
								// clone the existing original copy
								newPath = cache.cloneFile(path, root);
							}
						}
					}
					if (newPath == null) {
						//no space in cache
						return Errors.ENOMEM;
					}
					file = new RandomAccessFile(root.concat("/").concat(newPath), "rw");
					fdTable.put(++fd, new FileObject(file, 1, newPath));
					break;
				case CREATE_NEW:
					if (len >= 0) {
						return Errors.EEXIST;
					}
					if (len == -2) {
						return Errors.EISDIR;
					}
					synchronized (cachelock) {
						f = new File(root.concat("/").concat(path));
						if (!f.exists()) {
							// Call to the server to fetch file
							createFile(path);
							newPath = cache.cloneFile(path, root);
							if (newPath == null) {
								return Errors.ENOMEM;
							}
						} else {
							return Errors.EEXIST;
						}
					}
					// Upload the file to server
					file = new RandomAccessFile(root.concat("/").concat(newPath), "rw");
					fdTable.put(++fd, new FileObject(file, 1, newPath));
					break;
				case WRITE:
					if (len == -1) {
						return Errors.ENOENT;
					}
					if (len == -2) {
						return Errors.EISDIR;
					}
					// Call to the server to fetch file
					synchronized (cachelock) {
						f = new File(root.concat("/").concat(path));
						if (!f.exists() || current_version != version) {
							if (noOfReaders > 0) {
								int retvalue = downloadFile(path, path.concat(Integer.toString(noOfClients)), len,
										version);
								if (retvalue < 0) {
									return Errors.ENOMEM;
								}
								cache.updateParentMetadata(path);
								newPath = path.concat(Integer.toString(noOfClients));
							} else {
								// Call to the server to fetch file
								int retvalue = downloadFile(path, path, len, version);
								if (retvalue < 0) {
									return Errors.ENOMEM;
								}
								newPath = cache.cloneFile(path, root);
							}
						} else {
							newPath = cache.cloneFile(path, root);
						}
						if (newPath == null) {
							return Errors.ENOMEM;
						}
					}
					file = new RandomAccessFile(root.concat("/").concat(newPath), "rw");
					fdTable.put(++fd, new FileObject(file, 1, newPath));
					break;
				case READ:
					String new_path1 = path;
					if (len == -1) {
						return Errors.ENOENT;
					}
					if (len == -2) { // is directory
						file = null;
						fdTable.put(++fd, new FileObject(file, 3, path));
						return fd;
					}
					synchronized (cachelock) {
						f = new File(root.concat("/").concat(path));
						if (!f.exists() || current_version != version) {
							if (FileVersionTable.containsKey(path)) {
								FileMetadata fm = FileVersionTable.get(path);
								if (fm.nextVersionName != null) { //There is a newer version on cache
									path = fm.nextVersionName;
									cache.addReader(path);
									file = new RandomAccessFile(root.concat("/").concat(path), "r");
									fdTable.put(++fd, new FileObject(file, 2, path));
									return fd;
								} else {
									current_version = fm.fileVersion;
									if (fm.readerCount > 0 && current_version != version) {
										//New version available, but reader on current
										new_path1 = path.concat(Integer.toString(noOfClients));
										fm.nextVersionName = new_path1;
										FileVersionTable.put(path, fm);
									}
								}
							}
							if (f.exists()) { 
								//file will be replaced account for free space
								cache.freeSpace += f.length();
							}
							// Call to the server to fetch file
							int retvalue = downloadFile(path, new_path1, len, version);
							if (retvalue < 0) {
								return Errors.ENOMEM;
							}
						}
						cache.addReader(new_path1);
					}
					file = new RandomAccessFile(root.concat("/").concat(new_path1), "r");
					fdTable.put(++fd, new FileObject(file, 2, path));
					break;
				}
				return fd;
			} catch (IllegalArgumentException e) {
				return Errors.EINVAL;
			} catch (FileNotFoundException e) {
				return Errors.ENOENT;
			} catch (SecurityException e) {
				return Errors.EPERM;
			} catch (Exception e) {
				return Errors.EPERM;
			}
		}
		
		/*
		 * close - close called by client
		 */
		public int close(int fd) {
			if (!fdTable.containsKey(fd)) {
				return Errors.EBADF;
			}
			if (fdTable.get(fd).option > 2) { 
				//trying to close directory
				return Errors.EISDIR;
			}
			String path_name = (fdTable.get(fd).path);
			System.err.println("CLOSING file - " + path_name);
			// If file was writers copy
			if (fdTable.get(fd).option == 1) {
				FileMetadata fm = FileVersionTable.get(path_name);
				String orig_name = fm.fileName;
				FileMetadata fmOrig = FileVersionTable.get(orig_name);
				File tmp = new File(root.concat("/").concat(path_name));
				int noOfReaders = fmOrig.readerCount;
				int version = fmOrig.fileVersion + 1;
				long old_length = fm.fileLength;
				long orig_length = fmOrig.fileLength;
				long length = tmp.length();
				if (noOfReaders == 0) { //no readers on master copy
					// replace the master copy with write copy
					File origFile = new File(root.concat("/").concat(orig_name));
					if (tmp.renameTo(origFile)) {
						tmp.delete();
						synchronized (cachelock) {
							cache.freeSpace += (old_length + orig_length);
							cache.freeSpace -= length;
							FileVersionTable.remove(path_name);
							path_name = orig_name;
						}
					}
					synchronized (cachelock) {
						cache.addMetadata(orig_name, version, length);
					}
				} else { // there is a reader on original file
					synchronized (cachelock) {
						cache.updateParentLatestVersion(orig_name, path_name);
						cache.updateVersion(path_name);
						cache.freeSpace += old_length;
						cache.freeSpace -= length;
					}
				}
				synchronized (cachelock) {
					uploadFile(orig_name, path_name);
					cache.removeWriter(orig_name);
				}
			}
			if (fdTable.get(fd).option == 2) { //file was the master copy
				FileMetadata fm = FileVersionTable.get(path_name);
				int noOfReaders = fm.readerCount - 1;
				// check if there is newer version on cache
				if (noOfReaders == 0 && fm.nextVersionName != null) {
					String newFileName = fm.nextVersionName;
					long length = fm.fileLength;
					FileMetadata newFile = FileVersionTable.get(newFileName);
					if (newFile.readerCount == 0) { 
						// none reading on the newer version
						File origFile = new File(root.concat("/").concat(path_name));
						File tmp = new File(root.concat("/").concat(newFileName));
						int version = newFile.fileVersion + 1;
						synchronized (cachelock) {
							if (tmp.renameTo(origFile)) { //rename success
								tmp.delete();
								cache.freeSpace += length;
							}
							FileVersionTable.remove(newFileName);
							cache.addMetadata(path_name, version, length);
						}
					}
				}
			}
			RandomAccessFile file = fdTable.get(fd).file;
			try {
				fdTable.remove(fd);
				if (FileVersionTable.containsKey(path_name))
					synchronized (cachelock) {
						cache.removeReader(path_name);
					}
				file.close();
				return 0;
			} catch (IOException e) {
				return Errors.EPERM;
			}
		}
		
		/*
		 * write - perform client write operations
		 */
		public long write(int fd, byte[] buf) {
			if (!fdTable.containsKey(fd)) {
				return Errors.EBADF;
			}
			if (fdTable.get(fd).option > 2) {
				return Errors.EISDIR;
			}
			if (fdTable.get(fd).option > 1) {
				return Errors.EBADF;
			}
			RandomAccessFile file = fdTable.get(fd).file;
			try {
				file.write(buf);
				return buf.length;
			} catch (IOException e) {
				return Errors.EPERM;
			}

		}
		
		/*
		 * read - performs client read operation
		 */
		public long read(int fd, byte[] buf) {
			if (!fdTable.containsKey(fd)) {
				return Errors.EBADF;
			}
			if (fdTable.get(fd).option > 2) {
				return Errors.EISDIR;
			}
			RandomAccessFile file = fdTable.get(fd).file;
			try {
				int ret_val = file.read(buf);
				if (ret_val == -1) {
					ret_val = 0;
				}
				return ret_val;
			} catch (IOException e) {
				return Errors.EPERM;
			} catch (NullPointerException e) {
				return Errors.EINVAL;
			}
		}
		
		/*
		 * lseek - performs client seek operation
		 */
		public long lseek(int fd, long pos, LseekOption o) {
			if (!fdTable.containsKey(fd)) {
				return Errors.EBADF;
			}
			if (fdTable.get(fd).option > 2) {
				return Errors.EISDIR;
			}
			RandomAccessFile file = fdTable.get(fd).file;
			long position;
			try {
				switch (o) {
				case FROM_START:
					position = pos;
					break;
				case FROM_CURRENT:
					position = file.length() + pos;
					break;
				case FROM_END:
					position = file.getFilePointer() + pos;
					break;
				default:
					return Errors.EINVAL;
				}
				file.seek(position);
				return 0;
			} catch (IOException e) {
				return Errors.EINVAL;
			}
		}
		
		/*
		 * deletes file on proxy and server
		 */
		public int unlink(String path) {
			Path tempFile = Paths.get(root.concat("/").concat(path));
			if (Files.isDirectory(tempFile)) {
				return Errors.EISDIR;
			}
			int unlink_ret = deleteFile(path);
			try {
				FileMetadata fm = FileVersionTable.get(path);
				if (fm.readerCount == 0 && fm.writerCount == 0) {
					synchronized (cachelock) {
						cache.freeSpace += fm.fileLength;
						cache.deleteFile(path);
						Files.delete(tempFile);
					}
				}
			} catch (Exception e) {
				System.err.println("Proxy unlink error!");
			}
			return unlink_ret;
		}

		/*
		 * clientdone - called when client is gone
		 * clear the fdTable enty if there is anything
		 */
		public void clientdone() {
			for (int fd : fdTable.keySet()) {
				RandomAccessFile file = fdTable.get(fd).file;
				String path_name = (fdTable.get(fd).path);
				try {
					fdTable.remove(fd);
					if (FileVersionTable.containsKey(path_name))
						synchronized (cachelock) {
							cache.removeReader(path_name);
							cache.removeWriter(path_name);
						}
					file.close();
				} catch (IOException e) {
					System.err.println("Error closing file");
				}
			}
			return;
		}

	}

	private static class FileHandlingFactory implements FileHandlingMaking {
		public FileHandling newclient() {
			return new FileHandler();
		}
	}

	/*
	 * launch the proxy with port number to connect with server, 
	 * cache directory and cache size passed as command line arguments
	 */
	public static void main(String[] args) throws IOException {
		System.out.println("Hello World");
		try {
			srv = (ServerService) Naming.lookup("//" + args[0] + ":" + args[1] + "/Server");
		} catch (NotBoundException e) {
			System.err.println("Proxy - not bound exception");
		}
		root = args[2];
		// Instantiate cache with max size
		cache = new CacheHandler(Integer.parseInt(args[3]));
		FileVersionTable = cache.FileVersionTable;
		System.err.println("Size of cache set to - " + cache.maxSize);
		(new RPCreceiver(new FileHandlingFactory())).run();
	}
}
