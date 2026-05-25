using api.Data.Entities;
using api.Features.Conversation;
using api.Features.Message;
using api.Features.User;
using AutoMapper;

namespace api.Profiles;

public class MappingProfile : Profile
{
    public MappingProfile()
    {
        CreateMap<User, UserDto>();

        CreateMap<Data.Entities.Message, MessageDto>()
            .ForMember(dest => dest.SenderUsername, opt => opt.MapFrom(src =>
                src.Sender != null ? src.Sender.Username : "[deleted]"))
            .ForMember(dest => dest.SenderSigningKey, opt => opt.MapFrom(src =>
                src.Sender != null ? src.Sender.SigningPublicKey : string.Empty));

        CreateMap<SendMessageDto, Data.Entities.Message>();

        CreateMap<Conversation, ConversationDto>()
            .ForMember(dest => dest.Participants, opt => opt.MapFrom(src =>
                src.Participants.Select(p => p.User.Username).ToList()));

        CreateMap<Conversation, ConversationItemDto>()
            .ForMember(dest => dest.Participants, opt => opt.MapFrom(src =>
                src.Participants.Select(p => p.User.Username).ToList()));
    }
}
