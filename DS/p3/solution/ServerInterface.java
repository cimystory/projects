import java.rmi.Remote;
import java.rmi.RemoteException;

public interface ServerInterface extends Remote {
	public void serverActive(int id, int func) throws RemoteException;
	public int startFunction(int a) throws RemoteException;
	public Cloud.FrontEndOps.Request removeRequest1() throws RemoteException;
	public void addRequest1(Cloud.FrontEndOps.Request r) throws RemoteException;
	public void endMServer(int id) throws RemoteException;
}
