#include <std_include.hpp>
#include "../dw_include.hpp"
#include "../content_store.hpp"
#include "../runtime_context.hpp"
#include "component/console/console.hpp"

namespace demonware
{
	namespace
	{
		struct upload_destination final : bdTaskResult
		{
			std::string token;

			void serialize(byte_buffer* buffer) override
			{
				buffer->write_string("http://" + std::string(content_store::hostname) + "/uploads/" + token);
				buffer->write_uint16(content_store::storage_server_id);
				buffer->write_string(token);
				buffer->write_uint64(0);
			}
		};

		struct file_id_result final : bdTaskResult
		{
			std::uint64_t id{};

			void serialize(byte_buffer* buffer) override
			{
				buffer->write_uint64(id);
			}
		};

		struct file_metadata_result final : bdTaskResult
		{
			content_store::file_record record;
			std::string owner_name;
			std::string url;

			void serialize(byte_buffer* buffer) override
			{
				buffer->write_uint64(record.id);
				buffer->write_uint32(record.created);
				buffer->write_uint32(record.modified);
				buffer->write_uint32(static_cast<unsigned int>(record.data.size()));
				buffer->write_uint64(record.owner);
				buffer->write_string(owner_name);
				buffer->write_uint16(record.slot);
				buffer->write_string(record.name);
				buffer->write_string(url);
				buffer->write_uint16(record.category);
				buffer->write_blob(record.metadata);
				buffer->write_uint32(0);
				content_store::write_tags(*buffer, record.tags);
				buffer->write_uint32(0);
				buffer->write_uint64(0);
			}
		};
	}

	bdContentStreaming::bdContentStreaming() : service(50, "bdContentStreaming")
	{
		this->register_task(2, &bdContentStreaming::listFilesByOwner);
		this->register_task(3, &bdContentStreaming::listAllPublisherFiles);
		this->register_task(5, &bdContentStreaming::preUpload);
		this->register_task(6, &bdContentStreaming::postUpload);
		this->register_task(8, &bdContentStreaming::removeFile);
		this->register_task(9, &bdContentStreaming::preDownload);
	}

	void bdContentStreaming::exec_task(service_server* server, const std::string& data)
	{
		try
		{
			service::exec_task(server, data);
		}
		catch (const std::exception& error)
		{
			console::error("[DW]: Paint job storage failed: %s\n", error.what());
			server->create_reply(this->task_id(), game::demonware::BD_LOBBY_INTERNAL_FAILURE).send();
		}
	}

	void bdContentStreaming::listFilesByOwner(service_server* server, byte_buffer* buffer) const
	{
		std::uint64_t owner{};
		unsigned int since{};
		unsigned short maximum{}, offset{}, category{};
		std::string name;
		if (!buffer->read_uint64(&owner) || !buffer->read_uint32(&since)
			|| !buffer->read_uint16(&maximum) || !buffer->read_uint16(&offset)
			|| !buffer->read_uint16(&category)
			|| (!buffer->has_only_zero_padding() && !buffer->read_string(&name, 127))
			|| !buffer->has_only_zero_padding())
		{
			server->create_reply(this->task_id(), game::demonware::BD_PARAM_PARSE_ERROR).send();
			return;
		}

		auto reply = server->create_reply(this->task_id());
		if (category == content_store::paintjob_category)
		{
			auto records = content_store::list_files(owner);
			std::erase_if(records, [&](const auto& record)
			{
				return record.modified < since || (!name.empty() && record.name != name);
			});
			std::sort(records.begin(), records.end(), [](const auto& left, const auto& right)
			{
				return left.modified != right.modified ? left.modified > right.modified : left.slot < right.slot;
			});
			const auto identity = runtime_context::get_snapshot();
			const auto end = std::min(records.size(), static_cast<size_t>(offset) + maximum);
			for (size_t i = offset; i < end; ++i)
			{
				// An empty URL makes the native client perform preDownload.
				auto result = std::make_unique<file_metadata_result>();
				result->record = std::move(records[i]);
				result->owner_name = identity ? identity->persona_name.substr(0, 63) : "";
				reply.add(result);
			}
		}
		reply.send();
	}

	void bdContentStreaming::preUpload(service_server* server, byte_buffer* buffer) const
	{
		content_store::file_record record;
		unsigned int size{};
		std::string locale;
		if (!buffer->read_string(&record.name, 127) || !buffer->read_uint16(&record.slot)
			|| !buffer->read_uint32(&size) || !buffer->read_uint16(&record.category)
			|| !buffer->read_blob(&record.metadata, 512) || !buffer->read_string(&locale, 31)
			|| !buffer->has_only_zero_padding())
		{
			server->create_reply(this->task_id(), game::demonware::BD_PARAM_PARSE_ERROR).send();
			return;
		}
		if (record.category != content_store::paintjob_category || !content_store::valid_slot(record.slot)
			|| size != content_store::paintjob_size)
		{
			server->create_reply(this->task_id(), game::demonware::BD_CONTENTSTREAMING_SERVER_NOT_CONFIGURED).send();
			return;
		}
		record.owner = runtime_context::get_local_user_id();
		auto result = std::make_unique<upload_destination>();
		result->token = content_store::begin_upload(std::move(record));
		if (result->token.empty())
		{
			server->create_reply(this->task_id(), game::demonware::BD_PERMISSION_DENIED).send();
			return;
		}
		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}

	void bdContentStreaming::postUpload(service_server* server, byte_buffer* buffer) const
	{
		content_store::file_record record;
		unsigned short storage_id{};
		unsigned int size{};
		std::string token, locale;
		if (!buffer->read_string(&record.name, 127) || !buffer->read_uint16(&record.slot)
			|| !buffer->read_uint16(&storage_id) || !buffer->read_string(&token, 127)
			|| !buffer->read_uint32(&size) || !buffer->read_uint16(&record.category)
			|| !buffer->read_blob(&record.metadata, 512) || !content_store::read_tags(*buffer, record.tags)
			|| !buffer->read_string(&locale, 31) || !buffer->has_only_zero_padding()
			|| storage_id != content_store::storage_server_id || size != content_store::paintjob_size)
		{
			server->create_reply(this->task_id(), game::demonware::BD_PARAM_PARSE_ERROR).send();
			return;
		}
		record.owner = runtime_context::get_local_user_id();
		const auto id = content_store::finish_upload(token, record);
		if (!id)
		{
			server->create_reply(this->task_id(), game::demonware::BD_CONTENTSTREAMING_FILE_NOT_AVAILABLE).send();
			return;
		}
		auto result = std::make_unique<file_id_result>();
		result->id = *id;
		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}

	void bdContentStreaming::removeFile(service_server* server, byte_buffer* buffer) const
	{
		unsigned short slot{};
		if (!buffer->read_uint16(&slot) || !buffer->has_only_zero_padding() || !content_store::valid_slot(slot))
		{
			server->create_reply(this->task_id(), game::demonware::BD_PARAM_PARSE_ERROR).send();
			return;
		}
		content_store::delete_file(slot);
		server->create_reply(this->task_id()).send();
	}

	void bdContentStreaming::preDownload(service_server* server, byte_buffer* buffer) const
	{
		std::uint64_t id{};
		unsigned int size{};
		if (!buffer->read_uint64(&id) || !buffer->read_uint32(&size) || !buffer->has_only_zero_padding())
		{
			server->create_reply(this->task_id(), game::demonware::BD_PARAM_PARSE_ERROR).send();
			return;
		}
		auto record = content_store::find_file(id);
		if (!record)
		{
			server->create_reply(this->task_id(), game::demonware::BD_CONTENTSTREAMING_FILE_NOT_AVAILABLE).send();
			return;
		}
		auto result = std::make_unique<file_metadata_result>();
		result->record = std::move(*record);
		result->url = content_store::download_url(id);
		const auto identity = runtime_context::get_snapshot();
		result->owner_name = identity ? identity->persona_name.substr(0, 63) : "";
		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}

	void bdContentStreaming::listAllPublisherFiles(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}
}
