/*
 * Cache.java - Implements the Database interface
 * passes through set and transaction request
 * */
import java.util.*;
import java.rmi.RemoteException;
import java.rmi.server.UnicastRemoteObject;

public class Cache extends UnicastRemoteObject implements Cloud.DatabaseOps{
    // cacheTable - caches data values
    public static Map<String,String> cacheTable = new HashMap<String, String>();
    public ServerLib SL;
    public Cloud.DatabaseOps defaultDB;
    // initialize servelib and defaultDB
    public Cache(ServerLib SLObj) throws RemoteException{
        this.SL = SLObj;
        this.defaultDB = SL.getDB();
    }
    //override get method - return value in DB    
    public String get(String key){
        if(cacheTable.containsKey(key)){ //if value in cache
            return cacheTable.get(key);
        }
        // else request from DB
        String value = null;
        try{
            value = defaultDB.get(key);
        }
        catch(RemoteException e){
            System.err.println("Cache - get remote exception");
        }
        //update value in cache
        cacheTable.put(key,value);
        return value;
    }
    //override set method - forwarded to defaultDB
    public boolean set(String key, String val, String auth){
        try{
            return defaultDB.set(key,val,auth);
        }
        catch(RemoteException e){
            System.err.println("Cache - set error");
            return false;
        }
    }
    //override transaction method - forwarded to defaultDB
    public boolean transaction(String item, float price, int qty){
        try{
            return defaultDB.transaction(item,price,qty);
        }
        catch(RemoteException e){
            System.err.println("Cache - transaction error");
            return false;
        }
    }
}
