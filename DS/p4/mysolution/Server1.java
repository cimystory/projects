/* Skeleton code for Server */
import java.util.*;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentHashMap;
public class Server implements ProjectLib.CommitServing {
	public static ProjectLib PL;
	// transaction counter 
	public static int collageID = 0; 
	public class imageObj{
		String imgList;
		int vote;
		imageObj(String name){
			this.imgList = name;
			this.vote = -1; // no vote
		}
	}
	public class transactionObj{
		int count; // number of Prepare messages sent
		String fileName; // name of collage
		byte[] img; // collage
		Map<String,imageObj> sourceTable; // userNodes and corresponding imgList
		transactionObj(int c, String filename, byte[] image, Map<String,imageObj> sTable){
			this.fileName = filename;
			this.count = c;
			this.img = image;
			this.sourceTable = sTable;
		}
	}

	public class ServerSendMessage extends Thread{
		int transactionID_t;
		String clientID_t;
		ServerSendMessage(String clientID1, int transID){
			this.clientID_t = clientID1;
			this.transactionID_t = transID;
		}
		public void run(){
		//	System.out.println("Thead called for sendMessage to " + this.clientID_t);
			try{
				Thread.sleep(4000);
			}
			catch(InterruptedException e){
			}
			transactionObj transObj = transactionTable.get(transactionID_t);
			int vote = transObj.sourceTable.get(clientID_t).vote;
		//	System.out.println("####### The vote is - " + vote);
		//	System.out.println("******** Printed from thread - " + transObj.fileName);
			if(vote == -1){
				System.out.println("**** prepare timed out message ****");
				AnnounceCommit ancCmt = new AnnounceCommit(transObj,0);
				ancCmt.start();
			}
		}
	}

	// Maps transactionID and transactionObj
	public static ConcurrentHashMap<Integer,transactionObj> transactionTable = new ConcurrentHashMap<Integer,transactionObj>();

	public void startCommit( String filename, byte[] img, String[] sources ) {
		System.out.println( "Server: Got request to commit "+filename );
		// Maps userNode and imgObj
		ConcurrentHashMap<String,imageObj> sourceTable = new ConcurrentHashMap<String,imageObj>();
		int transactionID =  collageID++;
		imageObj imgObj;
		String builder;
		// populate source table for collage
		for(String s: sources){
			String[] source = s.split(":");
			if(sourceTable.containsKey(source[0])){
				imgObj = sourceTable.get(source[0]);
				builder = imgObj.imgList;
				imgObj.imgList = builder +","+source[1];
				sourceTable.put(source[0],imgObj);
			}
			else{
				sourceTable.put(source[0],new imageObj(source[1]));
			}
		}
		// populate transaction table
		transactionTable.put(transactionID,new transactionObj(sourceTable.size(),filename,img,sourceTable));
		// send message to every unique userNode
		Iterator it = sourceTable.entrySet().iterator();
		while(it.hasNext()){
			Map.Entry imgEntry = (Map.Entry)it.next();
			imgObj = (imageObj) imgEntry.getValue();
			String msgBody = transactionID + ":" + imgObj.imgList;
			byte[] msgbuff = msgBody.getBytes();
			int len = msgbuff.length;
			int type = 1;
			byte[] intLen = ByteBuffer.allocate(4).putInt(len).array();
			byte[] intType = ByteBuffer.allocate(4).putInt(type).array();
			int len1 = len + 8 + img.length;
			System.out.println("SERVER ask messge - " + msgBody);
			byte[] finalString = new byte[len1];
			System.arraycopy(intType,0,finalString,0,4);
			System.arraycopy(intLen,0,finalString,4,4);
			System.arraycopy(msgbuff,0,finalString,8,len);
			System.arraycopy(img,0,finalString,len+8,img.length);
			ProjectLib.Message askUserMsg = new ProjectLib.Message(imgEntry.getKey().toString(), finalString);
			System.out.println("SERVER - length of image - "+ img.length);
			ServerSendMessage srvSM = new ServerSendMessage(imgEntry.getKey().toString(), transactionID);
			srvSM.start();
			PL.sendMessage(askUserMsg);
		}
	}
	public static void main ( String args[] ) throws Exception {
		if (args.length != 1) throw new Exception("Need 1 arg: <port>");
		Server srv = new Server();
		PL = new ProjectLib( Integer.parseInt(args[0]), srv );	
		// main loop
		while (true){
			ProjectLib.Message msg = PL.getMessage();
			processMsg(msg);
		}
	}
	public static void processMsg(ProjectLib.Message msg){
		String msgBody = new String(msg.body);
		System.out.println("Server process message - " + msgBody);
		String[] userMsg = msgBody.split(":");
		if(userMsg[0].equals("1")){
			int transID = Integer.parseInt(userMsg[1]);
			int userVote = Integer.parseInt(userMsg[2]);
			transactionObj transObj = transactionTable.get(transID); 
			imageObj imgObj = transObj.sourceTable.get(msg.addr);
			imgObj.vote = userVote;
			transObj.sourceTable.put(msg.addr,imgObj);
			if(userVote == 1){
				transObj.count--;
				if(transObj.count == 0){
					// commit image
					System.out.println("Server: commit image - " + transObj.fileName);
					try{
						FileOutputStream imgFileStream = new FileOutputStream(transObj.fileName);
						imgFileStream.write(transObj.img);
						imgFileStream.close();
					}
					catch(Exception ex){
						System.out.println("error in writing collage");
					}
					AnnounceCommit ancCmt = new AnnounceCommit(transObj, 1);
					ancCmt.start();
				}
			}
			else if(userVote == 0){ // abort transaction
				System.out.println("Server abort - " + transObj.fileName);
				AnnounceCommit ancCmt = new AnnounceCommit(transObj, 0);
				ancCmt.start();
			}
		}
		else if(userMsg[0].equals("2")) { // received ack from client
			System.out.println("SERVER - Recieved ack from - " + msg.addr);
		}
	
	}
	public static class AnnounceCommit extends Thread{
		transactionObj transObj;
		int status;
		AnnounceCommit(transactionObj transObj1, int stat){
			this.transObj = transObj1;
			this.status = stat;
		}
		//send commit or abort message to userNodes
		public void run(){
			Iterator it = this.transObj.sourceTable.entrySet().iterator();
			imageObj imgObj;
			int userNodeCount = this.transObj.sourceTable.size();
			while(it.hasNext()){
				Map.Entry imgEntry = (Map.Entry)it.next();
				imgObj = (imageObj) imgEntry.getValue();
				String msgBody = status + ":" + imgObj.imgList;
				byte[] msgbuff = msgBody.getBytes();
                        	int len = msgbuff.length;
	                        int type = 2;
        	                byte[] intType = ByteBuffer.allocate(4).putInt(type).array();
                	        int len1 = len + 4;
                        	byte[] finalString = new byte[len1];
	                        System.arraycopy(intType,0,finalString,0,4);
        	                System.arraycopy(msgbuff,0,finalString,4,len);
				System.out.println("Server - announce message - " + this.status + " " + msgBody);
				ProjectLib.Message tellUsrMsg = new ProjectLib.Message(imgEntry.getKey().toString(), finalString);
				PL.sendMessage(tellUsrMsg);
			}
		}
	}
}
