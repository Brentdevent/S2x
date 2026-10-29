#pragma once

namespace demonware
{
	class bdContentStreaming final : public service
	{
	public:
		bdContentStreaming();

		void exec_task(service_server* server, const std::string& data) override;

	private:
		void preUpload(service_server* server, byte_buffer* buffer) const;
		void postUpload(service_server* server, byte_buffer* buffer) const;
		void removeFile(service_server* server, byte_buffer* buffer) const;
		void preDownload(service_server* server, byte_buffer* buffer) const;
		void listFilesByOwner(service_server* server, byte_buffer* buffer) const;
		void listAllPublisherFiles(service_server* server, byte_buffer* buffer) const;
	};
}
