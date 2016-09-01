/* Server.java - implements the coordinator, front-end server 
 * and app-server
 * It extends UnicastRemoteObject as the coordinator will
 * be registered with the Cloud registry
 * */
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
    //Global request queue
    public static Queue<Cloud.FrontEndOps.Request> reqQ = new ConcurrentLinkedQueue<Cloud.FrontEndOps.Request>();
    //<VMID,VMFunciton> front-end and app-server
    public static Map<Integer, Integer> VMTable = new HashMap<Integer,Integer>();
    public static int noFServers = 0;
    public static int noMServers = 0;
    public static int noServers = 0;
    private static final int BOOTTIME = 3000;
    private static final int IDLETIME = 6000;
    // limit on number of servers
    private static final int DROPLMT1 = 7;
    private static final int DROPLMT2 = 5;
    // ratio to compare queue length and VM count
    private static final double DROPFRAC = 1.7;
    ServerLib SL;
    
    /* RMI function to increament count when
    *  server is runing
    */
    public void serverActive(int id, int type){
        if(type == 1){ //front-end server
            noFServers += 1;
        }
        if(type == 2){ //app-server
            noMServers +=1;
        }
    }
    /* RMI function to decrease count of active app-servers
    *  when the server is shutdown
    */ 
    public void endMServer(int id){
            noMServers -= 1;
            VMTable.remove(id);
    }
    /* Retrun role of VM with given id :  
     * front-end or app-server
     */
    public int startFunction(int id){
        return VMTable.get(id);
    }
    /* Pull are return request from global queue
     */
    public Cloud.FrontEndOps.Request removeRequest1(){
        try{
            return reqQ.remove();
        }
        catch(NoSuchElementException e){
            return null;
        }
    }
    /* RMI function - add request to global queue
     */
    public void addRequest1(Cloud.FrontEndOps.Request r){
        reqQ.add(r);
    }

    public static void main ( String args[] ) throws Exception {        
        if (args.length < 3) throw new Exception("Need 3 args: <cloud_ip> <cloud_port> <VM id>");
        Registry reg = LocateRegistry.getRegistry(args[0],Integer.parseInt(args[1]));
        ServerLib SL = new ServerLib( args[0], Integer.parseInt(args[1]));
        Server serverObj = new Server();
        try{
            //bind with same name to identify first run
            reg.bind("test",serverObj);
            SL.register_frontend();
            String bindserver = "VM"+args[2];
            reg.bind(bindserver,serverObj);
            int VMID;

            //front-end
            VMID = SL.startVM();
            VMTable.put(VMID,1);

            //app-server
            VMID = SL.startVM();
            VMTable.put(VMID,2);
            noServers += 1;

            //instantiate cache
            Cache cacheObj = new Cache(SL);
            reg.bind("cacheObj", cacheObj);

            long endTime = System.currentTimeMillis() + BOOTTIME;
            int noDrop = 0;
            //drop request unit an app-server is launched
            while(System.currentTimeMillis() < endTime && noMServers == 0){
                Cloud.FrontEndOps.Request r = SL.getNextRequest();
                SL.drop(r);
                noDrop +=1;
                //launch 2 or 1 new app-server
                if(noDrop > DROPLMT1){ 
                    VMID = SL.startVM();
                    VMTable.put(VMID,2);
                    noServers +=1;
                }
                if(noDrop > DROPLMT2){
                    VMID = SL.startVM();
                    VMTable.put(VMID,2);
                    noServers +=1;
                    noDrop = 0;
                }            
            }
            int diff = 0;
            //spin to pull request and check if scale-out is needed
            while(true){
                Cloud.FrontEndOps.Request r = SL.getNextRequest();
                //drop if queue long
                if(reqQ.size() > DROPFRAC *  noMServers){
                    SL.drop(r);
                }
                else{ //add to global queue
                    reqQ.add(r);
                }
                diff = reqQ.size() - noMServers;
                //queue length long - scale out needed
                if(diff > 0 && noMServers == noServers ){
                    //scale by difference in queue lenght and 
                    //number of running app-servers
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

        //VM is not a coordinator
        ServerInterface master = (ServerInterface) reg.lookup("VM1");
        int VMiden = Integer.parseInt(args[2]);
        int VMfunction = master.startFunction(VMiden);

        if(VMfunction == 1){ // front-end
            SL.register_frontend();
            //update active servercount
            master.serverActive(VMiden, VMfunction);
            while(true){
                Cloud.FrontEndOps.Request r = SL.getNextRequest();
                master.addRequest1(r); //add request to global queue
            }
         }

         else { // app-server
             Cloud.DatabaseOps cache = (Cloud.DatabaseOps) reg.lookup("cacheObj"); 
             long time1 = System.currentTimeMillis();
             long diff1 = 0;
             //update active servercount
             master.serverActive(VMiden, VMfunction);
             while(true){
                 try{
                     Cloud.FrontEndOps.Request r = master.removeRequest1();
                     if(r == null){ //no request returned from global queue
                         long time2 = System.currentTimeMillis();
                         diff1 = (time2 - time1);
                         if(diff1 > IDLETIME){ //no request for 6000ms
                             master.endMServer(Integer.parseInt(args[2]));
                             UnicastRemoteObject.unexportObject(serverObj,true);
                             System.exit(0);
                         }
                         continue; //else keep checking for request
                     }
                     SL.processRequest(r, cache);
                     time1 = System.currentTimeMillis(); //update last request process time
                 }
                 catch(UnmarshalException e){
                     UnicastRemoteObject.unexportObject(serverObj,true);
                     System.exit(0);
                 }
             }
        }
    }
}
