/* Sample code for basic Server */
import java.util.*;
import java.util.concurrent.*;
import java.rmi.registry.*;
import java.lang.Exception.*;
import java.rmi.AlreadyBoundException;
import java.rmi.RemoteException;
import java.rmi.UnmarshalException;
import java.rmi.server.UnicastRemoteObject;

public class Server extends UnicastRemoteObject implements ServerInterface{
        public Server () throws RemoteException{
        }
	public static Queue<Cloud.FrontEndOps.Request> reqQ = new ConcurrentLinkedQueue<Cloud.FrontEndOps.Request>();
	public static Map<Integer, Integer> VMTable = new HashMap<Integer,Integer>();
	public static int noFServers = 0;
	public static int noMServers = 0;
	public static int noServers = 0;
	public static int serverStarted = 0;
	ServerLib SL;
	public void serverActive(int id, int type){
		if(type == 1){
			noFServers += 1;
		}
		if(type == 2){
			noMServers +=1;
		}
		System.err.println("Value of NoMServer - "+ Integer.toString(noMServers));
	}
	public void endMServer(int id){
			noMServers -= 1;
			VMTable.remove(id);
	}
	public int startFunction(int a){
		return VMTable.get(a);
	}
	public Cloud.FrontEndOps.Request removeRequest1(){
		try{
			return reqQ.remove();
		}
		catch(NoSuchElementException e){
			return null;
		}
	}
	public void addRequest1(Cloud.FrontEndOps.Request r){
		reqQ.add(r);
	}
	public static void main ( String args[] ) throws Exception {		
		if (args.length < 3) throw new Exception("Need 3 args: <cloud_ip> <cloud_port> <VM id>");
		Registry reg = LocateRegistry.getRegistry(args[0],Integer.parseInt(args[1]));
		ServerLib SL = new ServerLib( args[0], Integer.parseInt(args[1]));
		Server serverObj = new Server();
		try{
			RemoteObj obj = new RemoteObjImpl(); 
			reg.bind("test",serverObj);
			SL.register_frontend();
			String bindserver = "VM"+args[2];
			reg.bind(bindserver,serverObj);
			int VMID;
			VMID = SL.startVM();
			VMTable.put(VMID,1);
			VMID = SL.startVM();
			VMTable.put(VMID,2);
			noServers += 1;
         		long endTime = System.currentTimeMillis() + 3000;
			while(System.currentTimeMillis() < endTime && noMServers == 0){
				Cloud.FrontEndOps.Request r = SL.getNextRequest();
				SL.drop(r);
				
			}
			while(true){
                                Cloud.FrontEndOps.Request r = SL.getNextRequest();
				if(reqQ.size() > noMServers){
					SL.drop(r);
				}
				else{
                                	reqQ.add(r);
				}
				int diff = reqQ.size() - noMServers;
				if(diff > 0 && noMServers == noServers ){
					for(int i = 0; i < diff; i++){	
	                        		VMID = SL.startVM();
        	                		VMTable.put(VMID,2);
                		        	noServers += 1;
					}
				}
			}
		}
		catch(AlreadyBoundException e){
		}
		ServerInterface master = (ServerInterface) reg.lookup("VM1");
		int VMiden = Integer.parseInt(args[2]);
		int VMfunction = master.startFunction(VMiden);
                if(VMfunction == 1){ // front-end
       	            SL.register_frontend();
		    master.serverActive(VMiden, VMfunction);
               	    while(true){
                       	Cloud.FrontEndOps.Request r = SL.getNextRequest();
                       	//add request to global queue
   		        master.addRequest1(r);
               	    }
               	}
               	else{ //middle-tier
		   long time1 = System.currentTimeMillis();
		   long diff1 = 0;
		   master.serverActive(VMiden, VMfunction);
	           while(true){
			try{
                        	Cloud.FrontEndOps.Request r = master.removeRequest1();
				if(r == null){
					long time2 = System.currentTimeMillis();
					diff1 = (time2 - time1);
					if(diff1 > 7000 && Integer.parseInt(args[2]) != 3){
						System.err.println("Shuting down server!!");
						System.err.println(diff1);
						master.endMServer(Integer.parseInt(args[2]));
						System.exit(0);
					}
				//UnicastRemoteObject.unexportObject(this,true);
					continue;
				}
                      		//Get request from Global queue
              	        	SL.processRequest(r);
				time1 = System.currentTimeMillis();
			}
			catch(UnmarshalException e){
			//	UnicastRemoteObject.unexportObject(this,true);
				System.exit(0);
			}
              	     }
                  }
	}
}
