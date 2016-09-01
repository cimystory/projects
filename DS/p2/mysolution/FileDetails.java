/*
 * object sent by server to client with basic details
 * of file
 */
import java.io.*;

public class FileDetails implements Serializable {
        public long file_status; // length of file if it exists
        public int file_version;

	FileDetails(long file_stat, int file_ver){
		this.file_status = file_stat;
		this.file_version = file_ver;
	}
}
