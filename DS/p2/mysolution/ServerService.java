/*
 * Remote interface extended by the Server
 */
import java.rmi.Remote;
import java.rmi.RemoteException;

public interface ServerService extends Remote {
	public FileDetails checkFile(String path) throws RemoteException;
	public byte[] getFile(String path, long offset) throws RemoteException;
	public void uploadFile( String path, byte[] buff, long offset, long buffLength) throws RemoteException;
	public void createFile(String path) throws RemoteException;
	public int deleteFile(String path) throws RemoteException;
}
