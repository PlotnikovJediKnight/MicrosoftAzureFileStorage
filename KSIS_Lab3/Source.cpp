#include <was/storage_account.h>
#include <was/file.h>
#include <cpprest/http_client.h>
#include <cpprest/filestream.h>
#include <wininet.h>
#include <string>
#include <stdexcept>

const wchar_t connectionStringCharArray[] = L"/???/";
const utility::string_t share_name = U("myroot");
const utility::string_t baseURI = L"/???/";
const utility::string_t sas_token = L"/???/";
const utility::string_t download_dir = L"E:/ksis3/downloads/";
const utility::string_t toupload_dir = L"E:/ksis3/toupload/";
const utility::string_t upload_sas_token = L"/???/";
const utility::string_t create_dir_sas_token = L"/???/";

using namespace web::http;
using namespace web::http::client;
using concurrency::streams::streambuf;
using concurrency::streams::basic_istream;
using concurrency::streams::file_stream;

void printMenu() {
	ucout << "====================Main Menu====================" << std::endl;
	ucout << "0. Print main menu" << std::endl;
	ucout << "1. Get storage tree" << std::endl;
	ucout << "2. GET <uri>" << std::endl;
	ucout << "3. DELETE <uri>" << std::endl;
	ucout << "4. PUT <filename> <folder_uri>" << std::endl;
	ucout << "5. COPY <uri_source> <uri_destination>" << std::endl;
	ucout << "6. MOVE <uri_source> <folder_uri>" << std::endl;
	ucout << "7. POST <folder_uri>" << std::endl;
	ucout << "8. EXIT" << std::endl;
}


void dirRecursive(azure::storage::cloud_file_client& file_client, azure::storage::cloud_file_directory dir) {
	try {
		auto list = dir.list_files_and_directories();
		for (auto it : list)
		{
			if (it.is_directory())
			{
				dirRecursive(file_client, dir.get_subdirectory_reference(it.as_directory().name()));
			}
			else if (it.is_file())
			{
				ucout << it.as_file().uri().path() << std::endl;
			}
		}
	}
	catch (azure::storage::storage_exception& e) {
		ucerr << e.what() << std::endl;
	}
}

void printStorageTree() {
	const utility::string_t
		storage_connection_string(connectionStringCharArray);

	azure::storage::cloud_storage_account storage_account =
		azure::storage::cloud_storage_account::parse(storage_connection_string);

	azure::storage::cloud_file_client file_client =
		storage_account.create_cloud_file_client();

	azure::storage::cloud_file_share share =
		file_client.get_share_reference(share_name);

	azure::storage::cloud_file_directory root_dir =
		share.get_root_directory_reference();

	dirRecursive(file_client, root_dir);
}

utility::string_t getFileName(const utility::string_t& str) {
	auto index = str.find_last_of(L"/");
	return utility::string_t(str.begin() + index, str.end());
}


pplx::task<void> performGET_REQ(const utility::string_t& subject)
{
	http_client client(baseURI);

	http_request request(methods::GET);
	try {
		request.set_request_uri(subject + sas_token);
		return client.request(request).then([&subject](http_response response)
			{
				std::wostringstream ss;
				ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
				std::wcout << ss.str();
				if (response.status_code() == status_codes::OK) {
					std::cout << "Reading stream..." << std::endl;
					auto bodyStream = response.body();
					auto fileStream = concurrency::streams::fstream::open_ostream(download_dir + getFileName(subject), std::ios::out | std::ios::binary).get();

					bodyStream.read_to_end(fileStream.streambuf()).wait();
					fileStream.close().wait();
					std::cout << "File uploaded!" << std::endl;
				}
			});
	}
	catch (web::uri_exception& e) {
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return pplx::task<void>([] {});
}

pplx::task<void> performDELETE_REQ(const utility::string_t& subject)
{
	http_client client(baseURI);

	http_request request(methods::DEL);
	try {
		request.set_request_uri(subject + sas_token);
		return client.request(request).then([&subject](http_response response)
			{
				std::wostringstream ss;
				ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
				std::wcout << ss.str();
				if (response.status_code() == status_codes::Accepted) {
					std::cout << "Request for deletion is accepted!" << std::endl;
				}
			});
	}
	catch (web::uri_exception& e) {
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return pplx::task<void>([] {});
}

utility::string_t getContentLengthAsStringT(utility::size64_t N) {
	std::wostringstream ss;
	ss << N;
	return ss.str();
}

pplx::task<void> allocateSpaceOnServer(const utility::string_t& subject, const utility::string_t& destination)
{
	return file_stream<unsigned char>::open_istream(toupload_dir + subject, std::ios::out | std::ios::binary).then([subject, destination](pplx::task<basic_istream<unsigned char>> previousTask)
		{
			try
			{
				auto fileStream = previousTask.get();

				http_client client(baseURI);
				http_request request(methods::PUT);

				request.set_request_uri(destination + subject + sas_token);
				request.headers().set_content_type(L"file");
				request.headers().set_content_length(0);
				request.headers().add(L"x-ms-type", L"file");
				request.headers().add(L"x-ms-file-permission", L"inherit");
				request.headers().add(L"x-ms-file-attributes", L"None");
				request.headers().add(L"x-ms-file-creation-time", L"now");
				request.headers().add(L"x-ms-file-last-write-time", L"now");
				request.headers().add(L"x-ms-content-length", getContentLengthAsStringT(fileStream.streambuf().size()));
				request.set_body(fileStream);
				
				return client.request(request).then([fileStream](pplx::task<http_response> previousTask)
					{
						fileStream.close();

						std::wostringstream ss;
						try
						{
							auto response = previousTask.get();
							ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
							if (response.status_code() == status_codes::Created) {
								std::cout << "Space for a file is allocated!" << std::endl;
							}
						}
						catch (const http_exception& e)
						{
							ss << e.what() << std::endl;
						}
						std::wcout << ss.str();
					});
			}
			catch (const std::system_error& e)
			{
				std::wostringstream ss;
				ss << e.what() << std::endl;
				std::wcout << ss.str();
				return pplx::task_from_result();
			}
		});
}

pplx::task<void> fillAllocatedSpaceWithData(const utility::string_t& subject, const utility::string_t& destination)
{
	return file_stream<unsigned char>::open_istream(toupload_dir + subject, std::ios::out | std::ios::binary).then([subject, destination](pplx::task<basic_istream<unsigned char>> previousTask)
		{
			try
			{
				auto fileStream = previousTask.get();

				http_client client(baseURI);
				http_request request(methods::PUT);

				utility::size64_t N = fileStream.streambuf().size();

				request.set_request_uri(destination + subject + upload_sas_token);
				request.headers().set_content_type(L"file");
				request.headers().set_content_length(N);
				request.headers().add(L"x-ms-type", L"file");
				request.headers().add(L"x-ms-file-permission", L"inherit");
				request.headers().add(L"x-ms-file-attributes", L"None");
				request.headers().add(L"x-ms-file-creation-time", L"now");
				request.headers().add(L"x-ms-file-last-write-time", L"now");
				request.headers().add(L"x-ms-content-length", getContentLengthAsStringT(N));
				request.headers().add(L"x-ms-write", L"update");
				request.headers().add(L"x-ms-range", L"bytes=0-" + getContentLengthAsStringT(N - 1));
				request.set_body(fileStream);

				return client.request(request).then([fileStream](pplx::task<http_response> previousTask)
					{
						fileStream.close();

						std::wostringstream ss;
						try
						{
							auto response = previousTask.get();
							ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
							if (response.status_code() == status_codes::Created) {
								std::cout << "Contents of the file have just been transferred!" << std::endl;
							}
						}
						catch (const http_exception& e)
						{
							ss << e.what() << std::endl;
						}
						std::wcout << ss.str();
					});
			}
			catch (const std::system_error& e)
			{
				std::wostringstream ss;
				ss << e.what() << std::endl;
				std::wcout << ss.str();

				return pplx::task_from_result();
			}
		});
}



pplx::task<void> performCOPY_REQ(const utility::string_t& subject, const utility::string_t& destination)
{
	http_client client(baseURI);

	http_request request(methods::PUT);
	request.headers().set_content_type(L"file");
	request.headers().add(L"x-ms-type", L"file");
	request.headers().add(L"x-ms-copy-source", baseURI + subject + sas_token);
	request.headers().add(L"x-ms-file-permission-copy-mode", L"source");

	try {
		request.set_request_uri(destination + sas_token);
		return client.request(request).then([](http_response response)
			{
				std::wostringstream ss;
				ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
				std::wcout << ss.str();

				if (response.status_code() == status_codes::Accepted) {
					std::cout << "The file has been copied!" << std::endl;
				}
			});
	}
	catch (web::uri_exception& e) {
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return pplx::task<void>([] {});
}


pplx::task<void> performREMOVE_REQ(const utility::string_t& subject, const utility::string_t& destination_folder)
{
	try {
		performCOPY_REQ(subject, destination_folder + getFileName(subject)).get();
		performDELETE_REQ(subject).get();
	}
	catch (web::uri_exception& e) {
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return pplx::task<void>([] {});
}

pplx::task<void> performPOST_REQ(const utility::string_t& directory)
{
	http_client client(baseURI);

	http_request request(methods::PUT);
	try {
		request.set_request_uri(directory + create_dir_sas_token);
		request.headers().add(L"x-ms-file-permission", L"inherit");
		request.headers().add(L"x-ms-file-attributes", L"None");
		request.headers().add(L"x-ms-file-creation-time", L"now");
		request.headers().add(L"x-ms-file-last-write-time", L"now");
		return client.request(request).then([](http_response response)
			{
				std::wostringstream ss;
				ss << L"Server returned status code " << response.status_code() << L"." << std::endl;
				std::wcout << ss.str();
				if (response.status_code() == status_codes::Created) {
					std::cout << "Folder has been created!" << std::endl;
				}
			});
	}
	catch (web::uri_exception& e) {
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return pplx::task<void>([] {});
}

std::string input;
int command = -1;

int main() {

	if (!InternetCheckConnection(baseURI.c_str(), FLAG_ICC_FORCE_CONNECTION, 0))
	{
		std::cerr << "Check your internet connection before proceeding!" << std::endl;
		return -1;
	}
	else {

		printMenu();

		while (true) {
			ucout << "Enter your command:" << std::endl;
			getline(std::cin, input);
			try {
				command = std::stoi(input);

				switch (command) {
				case 0:
					printMenu();
					break;
				case 1:
					printStorageTree();
					break;
				case 2: {
					std::cout << "Enter URI:" << std::endl;
					utility::string_t str;
					getline(ucin, str);
					performGET_REQ(str).get();
					break;
				}
				case 3: {
					std::cout << "Enter URI:" << std::endl;
					utility::string_t str;
					getline(ucin, str);
					performDELETE_REQ(str).get();
					break;
				}
				case 4: {
					std::cout << "Enter filename:" << std::endl;
					utility::string_t filename;
					getline(ucin, filename);

					std::cout << "Enter path:" << std::endl;
					utility::string_t path;
					getline(ucin, path);

					allocateSpaceOnServer(filename, path).get();
					fillAllocatedSpaceWithData(filename, path).get();
					break;
				}
				case 5: {
					std::cout << "Enter source uri:" << std::endl;
					utility::string_t filename;
					getline(ucin, filename);

					std::cout << "Enter destination uri:" << std::endl;
					utility::string_t path;
					getline(ucin, path);

					performCOPY_REQ(filename, path).get();
					break;
				}
				case 6: {
					std::cout << "Enter source uri:" << std::endl;
					utility::string_t filename;
					getline(ucin, filename);

					std::cout << "Enter folder uri:" << std::endl;
					utility::string_t path;
					getline(ucin, path);

					performREMOVE_REQ(filename, path).get();
					break;
				}
				case 7: {
					std::cout << "Enter folder uri:" << std::endl;
					utility::string_t path;
					getline(ucin, path);

					performPOST_REQ(path).get();
					break;
				}
				case 8: {
					return 0;
				}
				default:
					throw std::invalid_argument("Wrong argument!");
				}
			}
			catch (std::invalid_argument& inv) {
				std::cerr << inv.what() << std::endl;
			}
			catch (std::exception& e) {
				std::cerr << e.what() << std::endl;
			}
		}

		return 0;
	}
}
















/* Debug Strings
							//auto bodyStream = response.body();
							//auto fileStream = concurrency::streams::fstream::open_ostream(L"E:\logfile", std::ios::out | std::ios::binary).get();

							//bodyStream.read_to_end(fileStream.streambuf()).wait();
							//fileStream.close().wait();
*/