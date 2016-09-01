/* UserNode - nodes that contribue to the collage image
 * asks user for decision and passes response to server
 * maintains imageBlockTable to identify images already commited
 * or blocked by 2 phase commit
 * */
import java.util.*;
import java.io.File;
import java.io.FileWriter;
import java.io.FileInputStream;
import java.nio.ByteBuffer;

public class UserNode implements ProjectLib.MessageHandling {
	// node ID
	public final String myId;
	// message tyes
	public static int PREPARE = 1;
	public static int DECISION = 2;
	public static int VOTE = 1;
	public static int ACK = 2;
	public static int YES = 1;
	public static int NO = 0;

	public UserNode( String id ) {
		myId = id;
	}
	public static ProjectLib PL;
	// map for blocked and deleted images
	public static Map<String, Integer> imgBlockTable = new HashMap<String, Integer>();

	/* deliverMessage - callback function for messages sent to node
 	*  parse message - ask userAnswer and respond to Server
 	*/
	public boolean deliverMessage( ProjectLib.Message msgDlvd) {
		byte[] msg = msgDlvd.body;
		byte[] typeBytes = new byte[4];
		System.arraycopy(msg, 0, typeBytes, 0, 4);
		int type = ByteBuffer.wrap(typeBytes).getInt();
		if(type == PREPARE){ // PREPARE message
			byte[] lenBytes = new byte[4];
			System.arraycopy(msg, 4, lenBytes, 0, 4);
                	int len = ByteBuffer.wrap(lenBytes).getInt();
			
                        byte[] msgBytes = new byte[len];
                        System.arraycopy(msg, 8, msgBytes, 0, len);
                        String res = new String(msgBytes); 
			String msgRcvd[] = res.split(":");
			int imglen = msg.length - (len + 8);
			
			byte[] img = new byte[imglen];
			System.arraycopy(msg, len+8, img, 0, imglen);
			
			boolean userRes = false;
			ProjectLib.Message userVote;
			String userVoteBody;
			String[] imgList = msgRcvd[1].split(",");
			for(String imgName : imgList){
				// if image is already in use
				if(imgBlockTable.containsKey(imgName)){
					//sent vote as NO
					userVoteBody = VOTE+":"+msgRcvd[0]+":" + NO;
					userVote = new ProjectLib.Message("Server",userVoteBody.getBytes());
					PL.sendMessage(userVote);
					return true;
				}
				else{
					imgBlockTable.put(imgName,2); // 2 - blocked; 3 - deleted
				}
			}
			userRes = PL.askUser(img,imgList);
			if(userRes){
				userVoteBody = VOTE+":"+ msgRcvd[0] +":" + YES;
			}
			else{
				userVoteBody = VOTE+":"+ msgRcvd[0] +":" + NO;
				for(String imgName : imgList){
					imgBlockTable.remove(imgName);
				}
			}
			userVote = new ProjectLib.Message("Server",userVoteBody.getBytes());
			logTable();	
			PL.sendMessage(userVote);
		}
		if(type == DECISION){ // messages is voting decision
			int msglen = msg.length - 4;
                        byte[] msgBytes = new byte[msglen];
                        System.arraycopy(msg, 4, msgBytes, 0, msglen);
                        String res = new String(msgBytes);
                        String msgRcvd[] = res.split(":");
                    
			String[] imgList = msgRcvd[1].split(",");
			int status = Integer.parseInt(msgRcvd[0]);
			for(String imgName: imgList){
				if(status == YES){ //commit - delete image
						// set image status to deleted
						imgBlockTable.put(imgName,3);
						new File(imgName).delete();
				}
				else{ // free image if blocked - decision is to abort
					if(imgBlockTable.containsKey(imgName) && imgBlockTable.get(imgName) == 2)
						imgBlockTable.remove(imgName);
				}
			}
			// log file to disk
			logTable();
			String ackMsg = ACK+":"+ YES +":" + msgRcvd[2];
			ProjectLib.Message userAck = new ProjectLib.Message("Server", ackMsg.getBytes());
			PL.sendMessage(userAck);
		}
		return true;
	}
	
	/* instantiate userNode, register node with projectLib
	 * if log file exist create the imgBlockTable
	 *  */
	public static void main ( String args[] ) throws Exception {
		if (args.length != 2) throw new Exception("Need 2 args: <port> <id>");
		UserNode UN = new UserNode(args[1]);
		//read file
		File f = new File("logFile");
                if(f.exists() && !f.isDirectory()) {
			updateImgTable(f);
		}
		PL = new ProjectLib( Integer.parseInt(args[0]), args[1], UN );	
	}

	/* updateImgTable - read log file and update the imageBlockTable
 	*/
	public static void updateImgTable(File f){	
		try{
	        	FileInputStream fis = new FileInputStream(f);
        	        int contentLen = fis.available();
                	byte[] content = new byte[contentLen];
	                fis.read(content);	
	                String fileContent = new String(content);
                	String[] logs = fileContent.split(",");
	                for(String log : logs){
				String[] imgLog = log.split(" ");
				imgBlockTable.put(imgLog[0],Integer.parseInt(imgLog[1]));
			}
		}
		catch(Exception e){
			System.out.println("Exception in constructing table");
		}		
	}

	/* logTable - sringify imgBlockTable and write logs to 
 	*  file on disk
 	*/
	public static void logTable(){
		Iterator it = imgBlockTable.entrySet().iterator();
		String logString = "";
		while(it.hasNext()){
			Map.Entry imgEntry = (Map.Entry) it.next();
			logString = logString + imgEntry.getKey() + " " + imgEntry.getValue() + ",";
		}
		try{
			FileWriter logFile = new FileWriter("logFile");
			logFile.write(logString);
                        logFile.flush();
                        PL.fsync();
                        logFile.close();

                }
                catch (Exception e){
                        System.out.println("Exception while writing table");
                }
	}
}
