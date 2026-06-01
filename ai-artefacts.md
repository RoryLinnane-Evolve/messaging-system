# AI Artefacts — CS4455 EPIC Project

**Group:** 16 — `teamwfh`
**Project:** Secure Messaging Application

This file documents AI tool usage throughout the project for submission and interview purposes.

---

## Reflective Summary

### What worked well

_Update as the project progresses._

### What required manual correction or rejection

| # | AI suggestion | What we did instead | Reason |
|---|---|---|---|
| — | — | — | — |

### Critical evaluation examples

1. _TBD_
2. _TBD_
3. _TBD_

---

## AI Transcripts.

What I would like to address now is the web client. I think the first step to making this would be building out a   
html skeleton and then working from there. Could you build this out please?                                           
                                                                                                                      
● Before I build it, one design decision to make quickly: one file or two?                                            
                                                                                                                      
  - Two files — index.html (login + messaging) and verify.html (standalone verification). Cleaner separation, and     
  aligns with the spec saying the verify page is "accessible independently."                                          
  - One file — everything in index.html, sections shown/hidden with JavaScript. Simpler to serve.                     
                                                                                                                      
  I'd lean toward two files given the spec language. Does that work for you?

  Also confirming: Bootstrap 5 via CDN for styling — no build tools, easy to explain in the interview, looks
  professional. Agreed?

──────────────────────────────────────────────────────────────────────────────────────────────────────────────── ▪▪▪ ─
❯ I will go with the one file which only contains verifying page for now, the messaging client is optional per the spec, so I would like to build this standalone verification page first and then begin to address stretch goals if we have time going forward. Can you justify all implementations to me as well please so that I can cross check vs rubric to ensure we are hitting the mark.

Done. Here's a justification of every decision, rubric-mapped:
                                                                                          
  ---
  ethers.js v6 via CDN                                                                                                
  Used for two things: connecting to Sepolia via JsonRpcProvider (read-only, no wallet), and ethers.keccak256 +
  ethers.toUtf8Bytes for hashing. Justification: vetted library, widely used in Ethereum development, no build tooling
   required. Satisfies the spec's "vetted cryptographic libraries" requirement.

  Hash computation (computeLocalHash)
  Exactly mirrors BlockchainService.cs line 107-109: concatenate base64 ciphertext strings → UTF-8 encode → keccak256.
   If this doesn't match byte-for-byte, you'd get a false FAIL. This is the core of the verification logic.

  Read-only provider, no private key
  JsonRpcProvider only reads from the chain. getDigest is a Solidity view function — costs no gas, requires no wallet.
   The Infura key is embedded but only used for reads, which is acceptable for a public verification tool.

  Pass/fail display with timestamp
  Directly satisfies the rubric: "clear pass/fail fidelity result with timestamp information."

  Accessible independently
  It's a static HTML file — open it in any browser, no server needed. Satisfies: "accessible independently of the
  messaging application."

  ---
Next I want you to read the resources file. The resources were our plan to tackle it i.e. ideal scenario. I have been   
using the resources to guide through the project. What I want to do now is actually make sure I have everything       
implemented ( which I think I do ). So can you cross check the actual code vs the resources file and see what the     
gaps are?                                                                                                             

● Got it — let me read the actual code files I haven't seen yet and compare against what the resources say should be  
  there.                                                                                  
                                                                                                                      
● Read 5 files (ctrl+o to expand)             
                                                 
● Here's what I found comparing the actual code to the resources:                                                     
                                                                                                                      
  Confirmed implemented ✅ (resources were right)                                                                     
  - KeyStore at-rest encryption — fully implemented (KeyStore.cpp)                                                    
  - Full C++ CLI — all operations present (view, send, forward, download, delete, revoke, account settings)
  - MessageStore — good STL usage (std::vector, std::remove_if, std::copy_if, std::sort, std::count_if, std::find_if,
  lambdas throughout)
  - Blockchain batching + ConversationDigest — implemented                                                            
  - verify.html — created this session ✅
  - ai-artefacts.md — created this session ✅                                                                         
                  
  Confirmed missing from actual code ❌

  ┌────────────────────────┬──────────────────────────────────────────────────┬───────────────────────────────────┐
  │          Gap           │                      Where                       │              Impact               │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Sender authentication  │ Client.cpp — no crypto_sign_detached anywhere    │ O'Brien: 5% criterion, currently  │
  │                        │                                                  │ Acceptable band                   │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Input validation on    │ RegisterDto, LogInDto, SendMessageDto — no       │ Burkley: Improper Input           │
  │ DTOs                   │ [Required], [MaxLength] etc.                     │ Validation                        │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Rate limiting          │ Program.cs — no AddRateLimiter                   │ Burkley: Broken Authentication    │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Cryptographic design   │ Doesn't exist                                    │ O'Brien: 5% criterion             │
  │ document               │                                                  │                                   │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Pentest report         │ Doesn't exist                                    │ Burkley: 10 marks                 │
  ├────────────────────────┼──────────────────────────────────────────────────┼───────────────────────────────────┤
  │ Cover document         │ Doesn't exist                                    │ Submission requirement            │
  └────────────────────────┴──────────────────────────────────────────────────┴───────────────────────────────────┘

  That's the complete honest picture. Which do you want to tackle first?

✻ Cooked for 16m 52s                 
                                                                                          
❯ We can go through them in order I guess. So shall we start with sender authentication?                              
                                                                                         
● Yes. Let me lay out exactly what needs to change before touching anything, so you understand each step.             
                                                                                                                      
  The plan — Option A (Ed25519 signatures):                                                                           
                                                                                                                      
  1. KeyStore — generate an Ed25519 signing keypair alongside the existing X25519 keypair, encrypt both to disk       
  2. Client::signUp — send both public keys to the server                                                             
  3. Client::sendMessage — after encrypting, sign (ciphertext || nonce || ephemeralPublicKey) with
  crypto_sign_detached                                                                                                
  4. Client::decryptMessage — after decrypting, fetch sender's signing key (TOFU), verify signature with              
  crypto_sign_verify_detached                                                                                         
  5. Server — store signingPublicKey on the User entity, expose it in UserDto, accept signature on SendMessageDto,
  store it on Message
  6. Database — new EF Core migration for the two new columns

  That's 6 areas of change. Let me read the files I haven't seen yet before we touch anything.

● Read 5 files (ctrl+o to expand)    
                                                       
● Good picture. Let me read AuthService.cs before we start touching anything.
                                                    
● Read 1 file (ctrl+o to expand)                                                          
                                                                                          
● Good, I have everything I need. Let me now walk you through exactly what will change in each file before touching
  anything.                                                                                                           
                                                                                                                      
  Server (4 files + 1 migration):                                                                                     
  - User.cs — add SigningPublicKey column                                                                             
  - RegisterDto — add SigningPublicKey field                                                                          
  - AuthService.cs — store it on registration                                                                         
  - UserDto.cs — expose it so the C++ client can fetch it                                                             
  - MessageDtos.cs + Message.cs — add Signature field (nullable, so existing messages don't break)                    
  - New migration                                                                                                     
                                                                                                                    
  C++ client (4 files):
  - KeyStore.h/.cpp — generate Ed25519 keypair alongside X25519, encrypt both together. Note: the key file format
  changes (136 → 232 bytes), so any existing ~/.securemsg_keys.bin needs to be deleted and regenerated
  - Client.h — add signingPublicKeyB64(), add second TOFU map for signing keys
  - Client.cpp — sign on send, verify on receive, send signing key on signup

  One question before we start: the User struct in the C++ client (include/User.h) — does it have a field for the
  public key? Let me check quickly.

● Read 1 file (ctrl+o to expand)     
                                                                                          
● And the Message struct:            
                                                                                          
● Read 1 file (ctrl+o to expand)                                                          
                                                                                          
● Got everything I need. Here's the full picture before we touch a single file:                                       
                                                                                          
  Files changing:                                                                                                     
  - User.h — add signingPublicKey                                                                                     
  - Message.h — add signature                                                                                         
  - KeyStore.h/.cpp — Ed25519 keypair added, key file format changes (136 → 232 bytes, existing key files need to be  
  deleted)                                                                                                            
  - Client.h/.cpp — sign on send, verify on receive, second TOFU map for signing keys                                 
  - User.cs, AuthDtos.cs, AuthService.cs, UserDtos.cs — SigningPublicKey added throughout                             
  - Message.cs, MessageDtos.cs — nullable Signature field added                                                       
  - New EF Core migration                                                                                           

  Shall I go ahead and implement all of this? It's a lot of files but each change is surgical — nothing is being
  redesigned, just extended.
 
 Go for it

 ● That's everything. Here's a summary of all changes made:
                                                                                          
  ---
  Sender authentication — complete                                                                                    
   
  ┌──────────────────────────────────────────────────────────┬─────────────────────────────────────────────────────┐  
  │                           File                           │                       Change                        │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ cpp-client/include/User.h                                │ Added signingPublicKey field                        │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ cpp-client/include/Message.h                             │ Added signature field                               │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ cpp-client/include/KeyStore.h                            │ Added SignPK/SignSK types, accessors, updated       │
  │                                                          │ on-disk format comment (136 → 232 bytes)            │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │                                                          │ Generates Ed25519 keypair alongside X25519;         │
  │ cpp-client/src/KeyStore.cpp                              │ encrypts both secret keys together; reads/writes    │
  │                                                          │ new 232-byte format                                 │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │                                                          │ Added signingPublicKeyB64(), _signTofu,             │
  │ cpp-client/include/Client.h                              │ _signTofuPath, verifyOrPinSignKey(),                │
  │                                                          │ loadSignTofu(), saveSignTofu()                      │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │                                                          │ Signs (ct‖nonce‖epk) raw bytes in encryptFor;       │
  │ cpp-client/src/Client.cpp                                │ verifies in decryptMessage if key is pinned; sends  │
  │                                                          │ signingPublicKey at sign-up; pins signing key in    │
  │                                                          │ getUser; parses signature from JSON                 │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Data/Entities/User.cs                                │ Added required string SigningPublicKey              │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Features/Auth/AuthDtos.cs                            │ Added SigningPublicKey to RegisterDto               │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Features/Auth/AuthService.cs                         │ Stores SigningPublicKey on registration             │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Features/User/UserDtos.cs                            │ Added SigningPublicKey to UserDto                   │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Data/Entities/Message.cs                             │ Added nullable string? Signature                    │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Features/Message/MessageDtos.cs                      │ Added nullable string? Signature to both MessageDto │
  │                                                          │  and SendMessageDto                                 │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Migrations/20260601000000_AddSenderAuthentication.cs │ New migration: adds SigningPublicKey to Users,      │
  │                                                          │ Signature to Messages                               │
  ├──────────────────────────────────────────────────────────┼─────────────────────────────────────────────────────┤
  │ api/Migrations/AppDbContextModelSnapshot.cs              │ Updated snapshot to reflect both new columns        │
  └──────────────────────────────────────────────────────────┴─────────────────────────────────────────────────────┘

  The AutoMapper MappingProfile.cs needs no changes — convention-based mapping picks up the new properties
  automatically.

  The one thing you'll need to do before first run: delete your existing ~/.securemsg_keys.bin on any test machines,
  since the on-disk format has changed. Re-registering will generate the new 232-byte file with both keypairs.

  Cool. So what was the next thing we had to address after sender authentication? I believe it was input validation   
on improper input?

Done. Quick summary:              
                                                                                          
  ┌───────────────────┬────────────────────────────────────────────────────────────────────────────────────────────┐  
  │        DTO        │                                       What was added                                       │  
  ├───────────────────┼────────────────────────────────────────────────────────────────────────────────────────────┤  
  │ RegisterDto       │ [Required] + [MinLength(3)] + [MaxLength(50)] on Username; [Required] + [MinLength(8)] on  │  
  │                   │ Password; [Required] on both key fields                                                    │
  ├───────────────────┼────────────────────────────────────────────────────────────────────────────────────────────┤
  │ LogInDto          │ [Required] on Username and Password                                                        │
  ├───────────────────┼────────────────────────────────────────────────────────────────────────────────────────────┤
  │ SendMessageDto    │ [Required] on ConversationId, Ciphertext, Nonce, EphemeralPublicKey                        │
  ├───────────────────┼────────────────────────────────────────────────────────────────────────────────────────────┤
  │ ChangePasswordDto │ [Required] on both; [MinLength(8)] on NewPassword                                          │
  └───────────────────┴────────────────────────────────────────────────────────────────────────────────────────────┘

  Because [ApiController] is on all controllers, any request that fails these constraints gets an automatic 400 Bad
  Request with a structured error body — no extra code needed.

  Next on the list was rate limiting on the auth endpoints. Want to tackle that now?

   Yeah lets go for it!                                                                                                

● Read 1 file (ctrl+o to expand)

● Good — .NET 10 has built-in rate limiting (Microsoft.AspNetCore.RateLimiting), no extra packages needed. The plan:

  1. Register a named "auth" sliding-window policy in Program.cs — 10 requests per minute per IP
  2. Add app.UseRateLimiter() to the middleware pipeline
  3. Apply [EnableRateLimiting("auth")] to the auth controller's login and register actions

  Let me check the auth controller first:

  Done. Here's what was put in place and why:
                                                                                          
  Sliding window rather than fixed window — a fixed window can be gamed by bursting 10 requests at 0:59 and another 10
   at 1:00. A sliding window with 6 segments (10-second buckets) distributes the count smoothly across the full       
  minute, closing that gap.
                                                                                                                      
  10 requests/minute per IP — generous enough for legitimate use (you'd never need to log in 10 times in a minute), 
  tight enough to make password brute-forcing pointless (600 attempts/hour vs. the billions needed).

  QueueLimit = 0 — rejected immediately with a 429. No queuing means no memory pressure from a flood of waiting
  requests.

  Only sign-up and login are rate-limited — logout is a harmless no-op (just a 204) so limiting it would add friction
  with no security gain.