/* Server - The coordinator of two phase commit 
 * implements ProjectLib.CommitServing that defines callback
 * function to start commit of candidate image
 * */
import java.util.*;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.io.File;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentHashMap;

public class Server implements ProjectLib.CommitServing {
	public static ProjectLib PL;
	private static final Object lock = new Object();
	// unique transaction ID 
	public static int collageID = 0;
	// expected RTT (time to timeout)
	public static int RTT = 6500;
	// server message type
	public static int DECISION = 2;
	public static int PREPARE = 1;

	// store details of each unique contributing userNode
	public static class imageObj{
		String imgList; //list of images in the source
		int vote;
		int ack;
		imageObj(String name){
			this.imgList = name;
			this.vote = -1; //no vote
			this.ack = -1; //no ack
		}
		imageObj(String name, int ack){
			this.imgList = name;
			this.ack = ack;
		}
	}

	// object for details of each collage (two-phase commit transaction)
	public static class transactionObj{
		int transactionID; //unique ID
		int count; // total number of Prepare messages sent
		String fileName; // name of collage
		byte[] img; // collage
		int isCommitted; // if img is committed
		Map<String,imageObj> sourceTable; // contributing userNodes and corresponding imgList
		transactionObj(int transID, int c, String filename, byte[] image, Map<String,imageObj> sTable){
			this.fileName = filename;
			this.count = c;
			this.transactionID = transID;
			this.img = image;
			this.sourceTable = sTable;
			this.isCommitted = 0;
		}
	}

	/* ServerSendMessage - check if response from userNode
 	* recieved within the expected RTT
 	* call abort if no response from client
 	* Maintain unique thread for every userNode
 	*/
	public class ServerSendMessage extends Thread{
		int transactionID_t;
		String clientID_t; //userNode ID
		ServerSendMessage(String clientID, int transID){
			this.clientID_t = clientID;
			this.transactionID_t = transID;
		}
		public void run(){
			try{
				Thread.sleep(RTT);
			}
			catch(InterruptedException e){
			}
			// check vote of userNode
			transactionObj transObj = transactionTable.get(transactionID_t);
			int vote = transObj.sourceTable.get(clientID_t).vote;
			if(vote == -1){ // Prepare message timed-out
				announceCommit(transObj,0);
			}
		}
	}

	// Maps each transactionID and transactionObj
	public static ConcurrentHashMap<Integer,transactionObj> transactionTable = new ConcurrentHashMap<Integer,transactionObj>();

	/* startCommit - call backk function to start commit of candidate image
 	*/
	public void startCommit( String filename, byte[] img, String[] sources ) {

		// Maps userNode and imgObj for every unique contributing userNode
		ConcurrentHashMap<String,imageObj> sourceTable = new ConcurrentHashMap<String,imageObj>();
		int transactionID =  collageID++;
		imageObj imgObj;
		String builder;
		// populate sourceTable for collage
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
		transactionTable.put(transactionID,new transactionObj(transactionID,sourceTable.size(),filename,img,sourceTable));

		// append transaction to file
		// only need to append current transaction entry as nothing else changed
		Iterator it = sourceTable.entrySet().iterator();
		String sourceBuilder = "";
		String logBuilder = "";
		while(it.hasNext()){
			// sourceTable entries delimited by &
			Map.Entry imgEntry = (Map.Entry)it.next();
			imgObj = (imageObj) imgEntry.getValue();
			sourceBuilder = sourceBuilder + imgEntry.getKey() + " " + imgObj.imgList + " " + imgObj.ack + "&"; 
		}
		// format - <trans_ID>:<commit_status>:<sourceTable_entries>;
		logBuilder = transactionID + ":0:" + sourceBuilder + ";" ;
		// sychronized access to file
		synchronized(lock) {
			try{
				FileOutputStream logFileStream = new FileOutputStream("logFile",true);
                	        logFileStream.write(logBuilder.getBytes());
				logFileStream.flush();
				PL.fsync();
        	        	logFileStream.close();
			}
			catch (Exception e){
				System.out.println("Exception - append file");
			}
		}
		// send message to every unique userNode
		// formate - <message_type> <message_length> <message_body> <collage_img_bytes>
		it = sourceTable.entrySet().iterator();
		while(it.hasNext()){
			Map.Entry imgEntry = (Map.Entry)it.next();
			imgObj = (imageObj) imgEntry.getValue();
			String msgBody = transactionID + ":" + imgObj.imgList;
			byte[] msgbuff = msgBody.getBytes();
			int len = msgbuff.length;
			int type = PREPARE;
			byte[] intLen = ByteBuffer.allocate(4).putInt(len).array();
			byte[] intType = ByteBuffer.allocate(4).putInt(type).array();
			int len1 = len + 8 + img.length;
			byte[] finalString = new byte[len1];
			System.arraycopy(intType,0,finalString,0,4);
			System.arraycopy(intLen,0,finalString,4,4);
			System.arraycopy(msgbuff,0,finalString,8,len);
			System.arraycopy(img,0,finalString,len+8,img.length);
			ProjectLib.Message askUserMsg = new ProjectLib.Message(imgEntry.getKey().toString(), finalString);
			ServerSendMessage srvSM = new ServerSendMessage(imgEntry.getKey().toString(), transactionID);
			// start timer to keep check of response time
			srvSM.start();
			PL.sendMessage(askUserMsg);
		}
	}
	
	/*
	* Instatiate server with the port number sent in cmd-line arg
	* register server with ProjectLib, check if log file present
	* update transactionTable from log file
	* loop to listen for messages sent to server
 	*/
	public static void main ( String args[] ) throws Exception {
		if (args.length != 1) throw new Exception("Need 1 arg: <port>");
		Server srv = new Server();
		PL = new ProjectLib( Integer.parseInt(args[0]), srv );
		File f = new File("logFile");
		if(f.exists() && !f.isDirectory()) {
			updateTransactionTable(f);
  		}	
		// continuously listen for messages
		while (true){
			ProjectLib.Message msg = PL.getMessage();
			processMsg(msg);
		}
	}

	/* updateTransactionTable - read logFile from disk, parse the content
 	*  and update transactionTable. Call abort or commit function respective
 	*  to the collage commit status
 	*/
	public static void updateTransactionTable(File f){
		try{
			FileInputStream logFIS = new FileInputStream(f);
        	        int contentLen = logFIS.available();
                	byte[] content = new byte[contentLen];
	                logFIS.read(content);
        	        String fileContent = new String(content);
			// parse string
                	String[] logs = fileContent.split(";"); //each transaction record seperated by ";"
        	        for(String log : logs){
                		String[] transLog = log.split(":");
                        	String[] imgLog = transLog[2].split("&"); //sourceTable entries delimited by &
	                        ConcurrentHashMap<String,imageObj> srcTableLog = new ConcurrentHashMap<String,imageObj>();
				// update source table for each transaction
                	        for(String imgLine : imgLog){
        	                        String[] imgContent = imgLine.split(" ");
	                                imageObj imgObjLog = new imageObj(imgContent[1],Integer.parseInt(imgContent[2]));
                                	srcTableLog.put(imgContent[0],imgObjLog);
                        	}
				// create transactionObj and update transactionTable
        	                transactionObj transObjLog = new transactionObj(Integer.parseInt(transLog[0]),0,null,null,srcTableLog);
                        	transObjLog.isCommitted = Integer.parseInt(transLog[1]);
        	                transactionTable.put(Integer.parseInt(transLog[0]),transObjLog);
				// update transaction counter to maintein uniqueness  
                        	if(transObjLog.transactionID > collageID){
                	                collageID = transObjLog.transactionID;
        	                }
				// distribute transaction decision to userNodes
                        	if(transObjLog.isCommitted == 1){
                	                announceCommit(transObjLog,1);
   				}                             
	                        else{ // abort if candidate image was not committed
                        		announceCommit(transObjLog,0);
                	        }
			}
		}
		catch(Exception e){
			System.err.println("Error reading log file");
		}
	}
	
	/* processMsg - process the message received form userNode
	 * processes user-Vote and acknowledgment of voting decision
 	* */
	public static void processMsg(ProjectLib.Message msg){
		String msgBody = new String(msg.body);
		String[] userMsg = msgBody.split(":");
		if(userMsg[0].equals("1")){ // userNode vote
			int transID = Integer.parseInt(userMsg[1]);
			int userVote = Integer.parseInt(userMsg[2]);
			transactionObj transObj = transactionTable.get(transID); 
			imageObj imgObj = transObj.sourceTable.get(msg.addr);
			imgObj.vote = userVote;
			transObj.sourceTable.put(msg.addr,imgObj);
			if(userVote == 1){ // user vote is YES
				transObj.count--;
				if(transObj.count == 0){ // all users have voted yes
					// commit image
					try{
						FileOutputStream imgFileStream = new FileOutputStream(transObj.fileName);
						imgFileStream.write(transObj.img);
						imgFileStream.close();
					}
					catch(Exception ex){
						System.out.println("error in writing collage");
					}
					transObj.isCommitted = 1;
					logTable();	
					announceCommit(transObj, 1);
				}
			}
			else if(userVote == 0){ // user vote is NO
				// abort transaction
				transObj.isCommitted = 0;
				logTable();
				System.out.println("Server abort - " + transObj.fileName);
				announceCommit(transObj, 0);
			}
		}
		else if(userMsg[0].equals("2")) { // acknowledgement messages
			int transID = Integer.parseInt(userMsg[2]);
                        transactionObj transObj = transactionTable.get(transID);
                        imageObj imgObj = transObj.sourceTable.get(msg.addr);
                        imgObj.ack = 1; // update acknoledgement status in transactionTable
                        transObj.sourceTable.put(msg.addr,imgObj);	
		}
	}

	/* logTable - stringfy transactionTable to log in logfile
 	* store details only required to send abort or commit
 	* messages to userNode
 	*
 	* format - <transactionEntry>;<transactionEntry>
 	* <transactionEntry> - trnsactionID:commit_status:<sourceTableEntry>&<sourceTableEntry>
	*/
	public static void logTable(){
		String tableBuilder = "";
		imageObj imgObj;
		// itereate through transaction table
		Iterator trans_it = transactionTable.entrySet().iterator();
		while(trans_it.hasNext()){
			Map.Entry transEntry = (Map.Entry)trans_it.next();
			transactionObj transObj = (transactionObj) transEntry.getValue();
			Iterator it = transObj.sourceTable.entrySet().iterator();
		        String sourceBuilder = "";
	                String logBuilder = "";
			// iterate through source userNode table
        	        while(it.hasNext()){
                	        Map.Entry imgEntry = (Map.Entry)it.next();
                        	imgObj = (imageObj) imgEntry.getValue();
	                        sourceBuilder = sourceBuilder + imgEntry.getKey() + " " + imgObj.imgList + " " + imgObj.ack + "&";
        	        }
                	logBuilder = transObj.transactionID + ":"+ transObj.isCommitted +":" + sourceBuilder + ";" ;
			tableBuilder = tableBuilder + logBuilder;
		}
		// synchronize writes to file
		synchronized(lock) {
                	try{
        	               	FileOutputStream logFileStream = new FileOutputStream("logFile",false);
		                logFileStream.write(tableBuilder.getBytes());
        	        	logFileStream.flush();
                	 	PL.fsync();
        	                logFileStream.close();
		         }
        		 catch (Exception e){
        	         	System.out.println("Exception while writing table");
		         }
		}
	}

	/* announceCommit - generate commit or abort message 
 	*  run independent thread to send message to each userNodes
 	*
 	*  format - <message_type> <message>
 	*  <message> - <decision>:<contributing img list>:<transactionID>
 	*/
	public static void announceCommit(transactionObj transObj, int status){
		Iterator it = transObj.sourceTable.entrySet().iterator();
		imageObj imgObj;
		int userNodeCount = transObj.sourceTable.size();
		while(it.hasNext()){
			Map.Entry imgEntry = (Map.Entry)it.next();
			imgObj = (imageObj) imgEntry.getValue();
			String msgBody = status + ":" + imgObj.imgList + ":" + transObj.transactionID;
			byte[] msgbuff = msgBody.getBytes();
                        int len = msgbuff.length;
                        int type = DECISION;
                        byte[] intType = ByteBuffer.allocate(4).putInt(type).array();
                        int len1 = len + 4;
                        byte[] finalString = new byte[len1];
                        System.arraycopy(intType,0,finalString,0,4);
                        System.arraycopy(msgbuff,0,finalString,4,len);
			ProjectLib.Message tellUsrMsg = new ProjectLib.Message(imgEntry.getKey().toString(), finalString);
			// call thread that sends msg until ack is received
			sendDecision sendDec = new sendDecision(tellUsrMsg, transObj.transactionID, imgEntry.getKey().toString());
			sendDec.start();
		}
	}
	
	/* sendDecision - unique thread for every userNode
 	* loops until ack received from userNode
 	*/
	public static class sendDecision extends Thread{
		ProjectLib.Message tellUsrMsg;
		int transID;
		String userID;
		sendDecision(ProjectLib.Message usrMsg, int transID, String userID){
			this.transID = transID;
			this.tellUsrMsg = usrMsg;
			this.userID = userID;
		}
		public void run(){
			// initialize ack with transactionTable value
			int ack = transactionTable.get(transID).sourceTable.get(userID).ack;
			// if ack not received send decision again
			while(ack == -1){
				PL.sendMessage(tellUsrMsg);
				// wait for estimated Round Trip Time
				try{
					Thread.sleep(RTT);
				}
				catch(Exception e){
					System.out.println("Exception in send decision");
				}
				ack = transactionTable.get(transID).sourceTable.get(userID).ack;
			}
		}
	}	
}
