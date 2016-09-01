/*
 * Server - Accessed by proxy using RMI calls 
 * Tasks - Returns fileDetails object
 * Enables file download and upload
 */

import java.io.*;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.rmi.RemoteException;
import java.rmi.Naming;
import java.rmi.registry.*;
import java.rmi.server.UnicastRemoteObject;
import java.util.concurrent.ConcurrentHashMap;

class Server extends UnicastRemoteObject implements ServerService{
	// Chunck size for file transfer
	public static long RMIWINDOW = 1024 * 1024;
	public static String dirroot;
	public Server() throws RemoteException{
		super(0);
	}
	// Table that maintains version of files
	static ConcurrentHashMap <String, Integer> FileVersionTable = new ConcurrentHashMap <String, Integer> ();
	
	/*
	 * checkFile - returns file length, file version
	 * flag for errors
	 */
	public FileDetails checkFile(String path){
		long file_stat;
		int file_version = 0;
		File file = new File(dirroot.concat("/").concat(path));
		//System.out.println("1");
		if(!file.exists()) file_stat = -1;
		else if(file.isDirectory()) file_stat = -2;
		else{
			file_stat = file.length();
			if (FileVersionTable.containsKey(path)){
				file_version = FileVersionTable.get(path);
			}
			else{
				file_version = 1;
				FileVersionTable.put(path,1);
			}
		}
		FileDetails f_details = new FileDetails(file_stat,file_version);
		return f_details;
	}

    /*
     * sends a stream of bytes to client
     */
	public byte[] getFile(String path, long off) {
		System.err.println("2. Server getFile - " + dirroot.concat("/").concat(path));
		try{
		RandomAccessFile file = new RandomAccessFile(dirroot.concat("/").concat(path), "r");
		long len;
		if(RMIWINDOW < file.length() - off)
			len = RMIWINDOW;
		else
			len = file.length() - off;
		byte[] buff = new byte[(int)len];
			file.seek(off);
			file.read(buff);
			file.close();
			return buff;
		}
		catch (FileNotFoundException e){
			return null;
		}
		catch(Exception e){
			return null;
		}
	}
	
	/*
	 * Download and replace file from client
	 */
	public void  uploadFile(String path, byte[] buff, long off, long buffLen){
		try{
			System.err.println("Server receiving file - " + path);
			RandomAccessFile tempFile = new RandomAccessFile(dirroot.concat("/").concat(path), "rw");
			if(off == 0){
				tempFile.setLength(0);
			}
			tempFile.seek(off);
			tempFile.write(buff, 0, (int)buffLen);
			tempFile.close();
			if(buffLen < RMIWINDOW){
				int version = FileVersionTable.get(path) + 1;
				System.err.println("Write - version -" + version);
				FileVersionTable.put(path,version);
			}
			System.err.println("Server received file - " + path + "offset - " + off + "buffLen - " + buffLen);
		}
		catch(Exception e){
			System.err.println("6. Server - sendFile fileoutputstream exception!!");
		}
	}
	
	/*
	 * Creates a file on server
	 */
	public void createFile(String path) throws RemoteException{
		try{
			System.err.println("Path for creating a file - "+ dirroot.concat("/").concat(path));
			RandomAccessFile file = new RandomAccessFile(dirroot.concat("/").concat(path), "rw");
			FileVersionTable.put(path,1);
		} catch (FileNotFoundException e) {
			System.out.println("7. Server createFile - Create a new file error!");
		}
	}
	
	/*
	 * Deletes file from server
	 */
	public int deleteFile(String path) throws RemoteException{
		try {
			System.err.println("8. unlink " + path);
			Path tempFile = Paths.get(dirroot.concat("/").concat(path));
			if (Files.isDirectory(tempFile)) {
				return FileHandling.Errors.EISDIR;
			}
			Files.delete(tempFile);
			FileVersionTable.remove(path);
			return 0;
		} catch (NoSuchFileException e) {
			return FileHandling.Errors.ENOENT;
		} catch (IOException e) {
			return FileHandling.Errors.EPERM;
		}
	}
	/*
	 * launch the server from the port and root directory 
	 * passed as command line arguments
	 */
	public static void main(String[] args) throws IOException {
		System.out.println("Hello World");
		try{
			LocateRegistry.createRegistry(Integer.parseInt(args[0]));
		}
		catch(RemoteException e){
			System.out.println("locate Registry exception!");
		}
		Server rpcserver = new Server();
		Naming.rebind("//127.0.0.1:"+args[0]+"/Server", rpcserver);
		dirroot = args[1];
	}
}
