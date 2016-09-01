import java.io.*;

class hello{

    public static void main(String[] args) throws IOException{
        System.err.println("Hello World");
        RandomAccessFile raf = new RandomAccessFile("oldfile.txt", "rw");
        System.out.println("" +  raf.readLine());

        File oldfile =new File("oldfile.txt");
	File newfile =new File("newfile.txt");
			
	if(oldfile.renameTo(newfile)){
		System.out.println("Rename succesful");
	}else{
		System.out.println("Rename failed");
	}
	System.out.println(""+ raf.readLine());
    }

}
