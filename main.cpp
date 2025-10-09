#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <filesystem>
#include <fcntl.h>
#include "json.hpp"
#include <cstdlib>
#include <fstream>

#define CHUNK 512

//find search for a string in string std::string

void errorMessage(const std::string message){
	std::cout << message
	<< errno << std::endl;
	exit(EXIT_FAILURE);	
}

std::string clientRequest(int &socket){
	
	std::string request = "";
	char buffer[CHUNK];
	int valread;
	
	while(true){
		valread = read(socket, buffer, sizeof buffer);
		// for debug porpuses
		request.append(buffer,valread);
		//std::cout << request << std::flush;	
			// okay, the problem was that
			// we were reading from a socket after
			// it returns 0, so theres nothing more
			// to read and it was stuck
			// send something random to close the socket
		if(valread < CHUNK){
			break;
		}
	}

	return request;
}



std::string handleHeader(const int HTTPcode, const char *fileType, const int index_length){

        // initialize the variable and builds the header
        std::string header;
        header = "HTTP/1.1 " + std::to_string(HTTPcode);
        if(HTTPcode == 200)
                header += " OK\r\n";
        else if (HTTPcode == 404)
                header += " Not Found\r\n";

        header += "Content-Type: ";
        header += fileType;
        header += "\r\n";
        header += "Content-Length: " + std::to_string(index_length) + "\r\n";
        header += "Accept-Charset: UTF-8\r\n";
        header += "\r\n";

	return header;
}

void handleGET(int &socket, const char *filePATH, const int HTTPcode, const char *fileType){
	
	const int file = open(filePATH, O_RDONLY);
	if(file < 0)
		errorMessage("on open file: ");

	const int index_length = std::filesystem::file_size(filePATH);
	const std::string header = handleHeader(HTTPcode, fileType, index_length);

	char buffer[CHUNK];
	int valread;
	std::string response = "";
	response.append(header);
	
	while((valread = read(file, buffer, sizeof buffer)) > 0){
		response.append(buffer, valread);
	}

	memset(&buffer, 0, sizeof buffer);

	send(socket, response.c_str(), response.size(), 0);
}

class ytDlpCommand {
	public: 
		std::string url, quality, format, path, index, isPlaylist;

		void concatAndExecYtDlp(){
			
			std::string ytDlpString = "yt-dlp ";
			bool needToZip = false;

			if(format.compare("mp4") == 0)
				ytDlpString += "-S " + quality + " ";
			if(format.compare("mp3") == 0)
				ytDlpString += "--audio-quality " + quality + " ";

			ytDlpString += "-t " + format + " ";

			if(index.compare("null") != 0 || isPlaylist.compare("false") == 0){
			
				if(index.compare("null") != 0)
					ytDlpString += "--no-playlist ";

				

				if(path.find("musicas") != -1){
					ytDlpString += "-o '" + path + "/%(title)s.%(ext)s' ";
					ytDlpString += "--write-thumbnail ";
				} else { 
			
					ytDlpString += "-o '" + path + "/video.%(ext)s' ";
					ytDlpString += "--write-thumbnail ";
				}

			} else if(isPlaylist.compare("true") == 0){

				ytDlpString += "--yes-playlist ";
				if(path.find("musicas") == -1){
					ytDlpString += "-o '" + path + "/playlist/%(playlist_index)s_%(title)s.%(ext)s' ";
					needToZip = true;
				} else {
					ytDlpString += "-o '" + path + "/%(playlist_title)s/%(playlist_index)s_%(title)s.%(ext)s' ";
				}
				
			}

			ytDlpString += "--embed-thumbnail ";
			ytDlpString += "--force-overwrites ";
			ytDlpString += "'" + url + "' ";

			// actually download the video
			std::cout << "download string: " << ytDlpString << std::endl;
			std::system(ytDlpString.c_str());

			// if true download the playlist :P
			if(needToZip){	
				std::string zipCommand = "cd " + path + " && 7z a playlist.zip ./playlist";
				std::system(zipCommand.c_str());
			}
		}
};

void handlePOST(int &socket, std::string request){
	
	int pos = request.find("\r\n\r\n");

	// parse only the body from the request
	std::string requestBody = request.substr(pos);

	// parse json request body
	ytDlpCommand downloadVideo;
	auto jsonData = nlohmann::json::parse(requestBody);
	downloadVideo.url = jsonData["urlValue"];
	downloadVideo.quality = jsonData["qualityValue"];
	downloadVideo.format = jsonData["formatValue"];
	downloadVideo.path = jsonData["pathValue"];
	downloadVideo.index = jsonData["index"];
	downloadVideo.isPlaylist = jsonData["isPlaylistUrl"];

	// download the video duh
	downloadVideo.concatAndExecYtDlp();

	// define variables for we start to form the response
	char buffer[CHUNK];
	int file_length;
	int valread;
	std::string hugeString = "";
	std::string bodyString = "";
	std::string typeOfVideo = "";
	std::string fileLength = "";
	std::string videoPathAndName = downloadVideo.path.append("/video.");
	
	if(downloadVideo.index.compare("null") != 0 || downloadVideo.isPlaylist.compare("false") == 0){
		if(downloadVideo.format.compare("mp4") == 0){ typeOfVideo += "video/mp4"; videoPathAndName.append("mp4"); }
		else if(downloadVideo.format.compare("mp3") == 0){ typeOfVideo +=" video/mp3"; videoPathAndName.append("mp3"); }
	} else {
		typeOfVideo += "application/zip";
		videoPathAndName = downloadVideo.path + "/playlist.zip";
	}

	std::ifstream file(videoPathAndName.c_str(), std::ios::binary);
	if(file){
		while(file.read(buffer, sizeof buffer)){
			bodyString.append(buffer, CHUNK);
		}
		file.close();
	}

	// handle with Content-Length
	file_length = bodyString.size();
	// handle header
	std::string header = handleHeader(200, typeOfVideo.c_str(), file_length);

	// append header abd then the body in only one string 
	hugeString.append(header);
	hugeString.append(bodyString);

	send(socket, hugeString.c_str(), hugeString.size(), 0);
}	

// core client handling
void threadFunc(int socket){
	
	std::string request = clientRequest(socket);
	
	if(strstr(request.c_str(), "GET / ")){
		handleGET(socket, "./public/index.html", 200, "text/html");
	} else if(strstr(request.c_str(), "GET /favicon.ico")){
		handleGET(socket, "./public/favicon.ico", 200, "image/x-icon");
	} else if(strstr(request.c_str(), "GET /style.css")){ 
		handleGET(socket, "./public/style.css", 200, "text/css");
	} else if(strstr(request.c_str(), "GET /fonts/RetroGaming.ttf")){
	       handleGET(socket, "./public/fonts/RetroGaming.ttf", 200, "application/octet-stream");	
	} else if(strstr(request.c_str(), "POST /video")) {
		handlePOST(socket, request);
	} else if(strstr(request.c_str(), "GET /thumbnail")){
		handleGET(socket, "./public/downloads/video.webp", 200, "image/webp");
	} else if(strstr(request.c_str(), "GET /NO_VIDEO_SUBMITED.png")) {
		handleGET(socket, "./public/NO_VIDEO_SUBMITED.png", 200, "image/png");
	} else {
		handleGET(socket, "./public/404.html", 404, "text/html");
	}

	close(socket);
}

int main(){
	
	int server_fd, client_fd;
	sockaddr_in server_addr;
	int server_addr_len = sizeof(server_addr);

	// IPV4 SOCKET
	if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		errorMessage("on server fd: ");

	//IPV4 ADDR
	server_addr.sin_family = AF_INET;
	// ANY IP CAN CONNECT
	server_addr.sin_addr.s_addr = INADDR_ANY;
	// PORT 6969 - HTONS MAKE IT A NETWORK BYTE
	server_addr.sin_port = htons(6969);

	// bind socket to port 
	if((bind(server_fd,
		(struct sockaddr*)&server_addr,
		sizeof(server_addr))) < 0)
		errorMessage("on bind: ");

	// listen on port
	if(listen(server_fd, 5) < 0)
		errorMessage("on listening: ");

	while(true){
		sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		// wait for socket acceptance 
		client_fd = accept(server_fd, 
				   (struct sockaddr*)&client_addr,
				   (socklen_t*)&client_addr_len);

		if(client_fd < 0)
			errorMessage("client fd error: ");

		std::thread client{threadFunc, client_fd};
		client.detach();
	}

	close(server_fd);
	return 0;
}
