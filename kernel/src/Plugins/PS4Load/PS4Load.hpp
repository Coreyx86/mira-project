#pragma once
#include <Utils/IModule.hpp>
#include <Utils/Types.hpp>

namespace Mira
{
		namespace Plugins
		{
				class PS4Load : public Mira::Utils::IModule
				{
				private:
					// Server address
					struct sockaddr_in m_Address;

					// Socket
					int32_t m_Socket;

					// Server port
					uint16_t m_Port;

					// Thread
					void* m_Thread;

					// Device path
					char m_Device[PATH_MAX];

					// Running
					volatile bool m_Running;
				public:
						PS4Load(uint16_t p_Port = 9997, char* p_Device = nullptr);
						virtual ~PS4Load();

						virtual const char* GetName() override { return "PS4Load"; }
						virtual bool OnLoad() override;
						virtual bool OnUnload() override;
						virtual bool OnSuspend() override;
						virtual bool OnResume() override;
				};
		}
}
