#include "SpacialData.h"

float SpacialData::altitude[] = { 0.f, 250.f, 500.f, 750.f, 1000.f, 1250.f, 1500.f, 1750.f, 2000.f, 2250.f, 2500.f, 2750.f, 3000.f, 3500.f, 4000.f, 4500.f, 5000.f, 5500.f, 6000.f, 6500.f, 7000.f, 7500.f, 8000.f, 8500.f, 9000.f, 9500.f, 10000.f, 10500.f, 11000.f, 11500.f, 12000.f, 12500.f, 13000.f, 13500.f, 14000.f };
float SpacialData::rlon[] = { 0.f, 0.00899287f, 0.01798574f, 0.02697861f, 0.03597148f, 0.04496435f, 0.05395722f, 0.06295009f, 0.07194296f, 0.08093583f, 0.0899287f, 0.09892157f, 0.10791444f, 0.11690731f, 0.12590018f, 0.13489304f, 0.14388593f, 0.15287879f, 0.16187166f, 0.17086454f, 0.1798574f, 0.18885027f, 0.19784313f, 0.20683601f, 0.21582888f, 0.22482175f, 0.23381463f, 0.2428075f, 0.25180036f, 0.26079324f, 0.2697861f, 0.27877897f, 0.28777185f, 0.2967647f, 0.30575758f, 0.31475046f, 0.3237433f, 0.3327362f, 0.34172907f, 0.35072193f, 0.3597148f, 0.36870766f, 0.37770054f, 0.38669342f, 0.39568627f, 0.40467915f, 0.41367203f, 0.42266488f, 0.43165776f, 0.44065064f, 0.4496435f, 0.45863637f, 0.46762925f, 0.4766221f, 0.485615f, 0.49460784f, 0.5036007f, 0.51259357f, 0.5215865f, 0.5305793f, 0.5395722f, 0.5485651f, 0.55755794f, 0.5665508f, 0.5755437f, 0.58453655f, 0.5935294f, 0.6025223f, 0.61151516f, 0.620508f, 0.6295009f, 0.6384938f, 0.6474866f, 0.65647954f, 0.6654724f, 0.67446524f, 0.68345815f, 0.692451f, 0.70144385f, 0.7104367f, 0.7194296f, 0.72842246f, 0.7374153f, 0.7464082f, 0.7554011f, 0.7643939f, 0.77338684f, 0.7823797f, 0.79137254f, 0.80036545f, 0.8093583f, 0.81835115f, 0.82734406f, 0.8363369f, 0.84532976f, 0.8543227f, 0.8633155f, 0.8723084f, 0.8813013f, 0.89029413f, 0.899287f, 0.9082799f, 0.91727275f, 0.9262656f, 0.9352585f, 0.94425136f, 0.9532442f, 0.9622371f, 0.97123f, 0.9802228f, 0.9892157f, 0.9982086f, 1.0072014f, 1.0161943f, 1.0251871f, 1.03418f, 1.043173f, 1.0521657f, 1.0611587f, 1.0701516f, 1.0791444f, 1.0881373f, 1.0971302f, 1.106123f, 1.1151159f, 1.1241088f, 1.1331016f, 1.1420945f, 1.1510874f, 1.1600802f, 1.1690731f, 1.178066f, 1.1870588f, 1.1960517f, 1.2050446f, 1.2140374f, 1.2230303f, 1.2320232f, 1.241016f, 1.250009f, 1.2590019f, 1.2679946f, 1.2769876f, 1.2859805f, 1.2949733f, 1.3039662f, 1.3129591f, 1.3219519f, 1.3309448f, 1.3399377f, 1.3489305f, 1.3579234f, 1.3669163f, 1.3759091f, 1.384902f, 1.3938948f, 1.4028877f, 1.4118806f, 1.4208734f, 1.4298663f, 1.4388592f, 1.447852f, 1.4568449f, 1.4658378f, 1.4748306f, 1.4838235f, 1.4928164f, 1.5018092f, 1.5108021f, 1.5197951f, 1.5287879f, 1.5377808f, 1.5467737f, 1.5557665f, 1.5647594f, 1.5737523f, 1.5827451f, 1.591738f, 1.6007309f, 1.6097237f, 1.6187166f, 1.6277095f, 1.6367023f, 1.6456952f, 1.6546881f, 1.6636809f, 1.6726738f, 1.6816667f, 1.6906595f, 1.6996524f, 1.7086453f, 1.7176381f, 1.726631f, 1.735624f, 1.7446167f, 1.7536097f, 1.7626026f, 1.7715954f, 1.7805883f, 1.7895812f, 1.798574f, 1.8075669f, 1.8165598f, 1.8255526f, 1.8345455f, 1.8435384f, 1.8525312f, 1.8615241f, 1.870517f, 1.8795098f, 1.8885027f, 1.8974956f, 1.9064884f, 1.9154813f, 1.9244742f, 1.933467f, 1.94246f, 1.9514527f, 1.9604456f, 1.9694386f, 1.9784313f, 1.9874243f, 1.9964172f, 2.00541f, 2.0144029f, 2.0233958f, 2.0323887f, 2.0413816f, 2.0503743f, 2.0593672f, 2.06836f, 2.077353f, 2.086346f, 2.0953388f, 2.1043315f, 2.1133244f, 2.1223173f, 2.1313102f, 2.1403031f, 2.149296f, 2.1582887f, 2.1672816f, 2.1762745f, 2.1852674f, 2.1942604f, 2.2032533f, 2.212246f, 2.2212389f, 2.2302318f, 2.2392247f, 2.2482176f, 2.2572103f, 2.2662032f, 2.275196f, 2.284189f, 2.293182f, 2.3021748f, 2.3111675f, 2.3201604f, 2.3291533f, 2.3381462f, 2.3471391f, 2.356132f, 2.3651247f, 2.3741176f, 2.3831105f, 2.3921034f, 2.4010963f, 2.4100893f, 2.419082f, 2.4280748f, 2.4370677f, 2.4460607f, 2.4550536f, 2.4640465f, 2.4730392f, 2.482032f, 2.491025f, 2.500018f, 2.5090108f, 2.5180037f, 2.5269964f, 2.5359893f, 2.5449822f, 2.553975f, 2.562968f, 2.571961f, 2.5809536f, 2.5899465f, 2.5989394f, 2.6079323f, 2.6169252f, 2.6259181f, 2.6349108f, 2.6439037f, 2.6528966f, 2.6618896f, 2.6708825f, 2.6798754f, 2.688868f, 2.697861f, 2.7068539f, 2.7158468f, 2.7248397f, 2.7338326f, 2.7428253f, 2.7518182f, 2.760811f, 2.769804f, 2.778797f, 2.7877896f, 2.7967825f, 2.8057754f, 2.8147683f, 2.8237612f, 2.8327541f, 2.8417468f, 2.8507397f, 2.8597326f, 2.8687255f, 2.8777184f, 2.8867114f, 2.895704f, 2.904697f, 2.9136899f, 2.9226828f, 2.9316757f, 2.9406686f, 2.9496613f, 2.9586542f, 2.967647f, 2.97664f, 2.985633f, 2.9946258f, 3.0036185f, 3.0126114f, 3.0216043f, 3.0305972f, 3.0395901f, 3.048583f, 3.0575757f, 3.0665686f, 3.0755615f, 3.0845544f, 3.0935473f, 3.1025403f, 3.111533f, 3.1205258f, 3.1295187f, 3.1385117f, 3.1475046f, 3.1564975f, 3.1654902f, 3.174483f, 3.183476f, 3.192469f, 3.2014618f, 3.2104547f, 3.2194474f, 3.2284403f, 3.2374332f, 3.246426f, 3.255419f, 3.264412f, 3.2734046f, 3.2823975f, 3.2913904f, 3.3003833f, 3.3093762f, 3.318369f, 3.3273618f, 3.3363547f, 3.3453476f, 3.3543406f, 3.3633335f, 3.3723261f, 3.381319f, 3.390312f, 3.3993049f, 3.4082978f, 3.4172907f, 3.4262834f, 3.4352763f, 3.4442692f, 3.453262f, 3.462255f, 3.471248f, 3.4802406f, 3.4892335f, 3.4982264f, 3.5072193f, 3.5162122f, 3.5252051f, 3.5341978f, 3.5431907f, 3.5521836f, 3.5611765f, 3.5701694f, 3.5791624f, 3.588155f, 3.597148f, 3.6061409f, 3.6151338f, 3.6241267f, 3.6331196f, 3.6421123f, 3.6511052f, 3.660098f, 3.669091f, 3.678084f, 3.6870768f, 3.6960695f, 3.7050624f, 3.7140553f, 3.7230482f, 3.7320411f, 3.741034f, 3.7500267f, 3.7590196f, 3.7680125f, 3.7770054f, 3.7859983f, 3.7949913f, 3.803984f, 3.8129768f, 3.8219697f, 3.8309627f, 3.8399556f, 3.8489485f, 3.8579412f, 3.866934f, 3.875927f, 3.88492f, 3.8939128f, 3.9029055f, 3.9118984f, 3.9208913f, 3.9298842f, 3.938877f, 3.94787f, 3.9568627f, 3.9658556f, 3.9748485f, 3.9838414f, 3.9928343f, 4.0018272f, 4.01082f, 4.019813f, 4.0288057f, 4.0377984f, 4.0467916f, 4.055784f, 4.0647774f, 4.07377f, 4.082763f, 4.091756f, 4.1007485f, 4.1097417f, 4.1187344f, 4.1277275f, 4.13672f, 4.145713f, 4.154706f, 4.1636987f, 4.172692f, 4.1816845f, 4.1906776f, 4.1996703f, 4.208663f, 4.217656f, 4.226649f, 4.235642f, 4.2446346f, 4.2536273f, 4.2626204f, 4.271613f, 4.2806063f, 4.289599f, 4.298592f, 4.307585f, 4.3165774f, 4.3255706f, 4.3345633f, 4.3435564f, 4.352549f, 4.3615417f, 4.370535f, 4.3795276f, 4.3885207f, 4.3975134f, 4.4065065f, 4.415499f, 4.424492f, 4.433485f, 4.4424777f, 4.451471f, 4.4604635f, 4.469456f, 4.4784493f, 4.487442f, 4.496435f, 4.505428f, 4.5144205f, 4.5234137f, 4.5324063f, 4.5413995f, 4.550392f, 4.5593853f, 4.568378f, 4.5773706f, 4.586364f, 4.5953565f, 4.6043496f, 4.6133423f, 4.622335f, 4.631328f, 4.640321f, 4.649314f, 4.6583066f, 4.6672997f, 4.6762924f, 4.685285f, 4.6942782f, 4.703271f, 4.712264f, 4.7212567f, 4.7302494f, 4.7392426f, 4.748235f, 4.7572284f, 4.766221f, 4.775214f, 4.784207f, 4.7931995f, 4.8021927f, 4.8111854f, 4.8201785f, 4.829171f, 4.838164f, 4.847157f, 4.8561497f, 4.865143f, 4.8741355f, 4.8831286f, 4.8921213f, 4.901114f, 4.910107f, 4.9191f, 4.928093f, 4.9370856f, 4.9460783f, 4.9550714f, 4.964064f, 4.9730573f, 4.98205f, 4.9910426f, 5.000036f, 5.0090284f, 5.0180216f, 5.0270143f, 5.0360074f, 5.045f, 5.0539927f, 5.062986f, 5.0719786f, 5.0809717f, 5.0899644f, 5.098957f, 5.10795f, 5.116943f, 5.125936f, 5.1349287f, 5.143922f, 5.1529145f, 5.161907f, 5.1709003f, 5.179893f, 5.188886f, 5.197879f, 5.2068715f, 5.2158647f, 5.2248573f, 5.2338505f, 5.242843f, 5.2518363f, 5.260829f, 5.2698216f, 5.278815f, 5.2878075f, 5.2968006f, 5.3057933f, 5.314786f, 5.323779f, 5.332772f, 5.341765f, 5.3507576f, 5.3597507f, 5.3687434f, 5.377736f, 5.3867292f, 5.395722f, 5.404715f, 5.4137077f, 5.4227004f, 5.4316936f, 5.440686f, 5.4496794f, 5.458672f, 5.467665f, 5.476658f, 5.4856505f, 5.4946437f, 5.5036364f, 5.5126295f, 5.521622f, 5.530615f, 5.539608f, 5.5486007f, 5.557594f, 5.5665865f, 5.575579f, 5.5845723f, 5.593565f, 5.602558f, 5.611551f, 5.620544f, 5.6295366f, 5.6385293f, 5.6475224f, 5.656515f, 5.6655083f, 5.674501f, 5.6834936f, 5.692487f, 5.7014794f, 5.7104726f, 5.7194653f, 5.7284584f, 5.737451f, 5.7464437f, 5.755437f, 5.7644296f, 5.7734227f, 5.7824154f, 5.791408f, 5.800401f, 5.809394f, 5.818387f, 5.8273797f, 5.836373f, 5.8453655f, 5.854358f, 5.8633513f, 5.872344f, 5.881337f, 5.89033f, 5.8993225f, 5.9083157f, 5.9173083f, 5.9263015f, 5.935294f, 5.9442873f, 5.95328f, 5.9622726f, 5.971266f, 5.9802585f, 5.9892516f, 5.9982443f, 6.007237f, 6.01623f, 6.025223f, 6.034216f, 6.0432086f, 6.0522017f, 6.0611944f, 6.070187f, 6.0791802f, 6.088173f, 6.097166f, 6.1061587f, 6.1151514f, 6.1241446f, 6.133137f, 6.1421304f, 6.151123f, 6.1601157f, 6.169109f, 6.1781015f, 6.1870947f, 6.1960874f, 6.2050805f, 6.214073f, 6.223066f, 6.232059f, 6.2410517f, 6.250045f, 6.2590375f, 6.26803f, 6.2770233f, 6.286016f };
float SpacialData::rlat[] = { 0.f, 0.00899287f, 0.01798574f, 0.02697861f, 0.03597148f, 0.04496435f, 0.05395722f, 0.06295009f, 0.07194296f, 0.08093583f, 0.0899287f, 0.09892157f, 0.10791444f, 0.11690731f, 0.12590018f, 0.13489304f, 0.14388593f, 0.15287879f, 0.16187166f, 0.17086454f, 0.1798574f, 0.18885027f, 0.19784313f, 0.20683601f, 0.21582888f, 0.22482175f, 0.23381463f, 0.2428075f, 0.25180036f, 0.26079324f, 0.2697861f, 0.27877897f, 0.28777185f, 0.2967647f, 0.30575758f, 0.31475046f, 0.3237433f, 0.3327362f, 0.34172907f, 0.35072193f, 0.3597148f, 0.36870766f, 0.37770054f, 0.38669342f, 0.39568627f, 0.40467915f, 0.41367203f, 0.42266488f, 0.43165776f, 0.44065064f, 0.4496435f, 0.45863637f, 0.46762925f, 0.4766221f, 0.485615f, 0.49460784f, 0.5036007f, 0.51259357f, 0.5215865f, 0.5305793f, 0.5395722f, 0.5485651f, 0.55755794f, 0.5665508f, 0.5755437f, 0.58453655f, 0.5935294f, 0.6025223f, 0.61151516f, 0.620508f, 0.6295009f, 0.6384938f, 0.6474866f, 0.65647954f, 0.6654724f, 0.67446524f, 0.68345815f, 0.692451f, 0.70144385f, 0.7104367f, 0.7194296f, 0.72842246f, 0.7374153f, 0.7464082f, 0.7554011f, 0.7643939f, 0.77338684f, 0.7823797f, 0.79137254f, 0.80036545f, 0.8093583f, 0.81835115f, 0.82734406f, 0.8363369f, 0.84532976f, 0.8543227f, 0.8633155f, 0.8723084f, 0.8813013f, 0.89029413f, 0.899287f, 0.9082799f, 0.91727275f, 0.9262656f, 0.9352585f, 0.94425136f, 0.9532442f, 0.9622371f, 0.97123f, 0.9802228f, 0.9892157f, 0.9982086f, 1.0072014f, 1.0161943f, 1.0251871f, 1.03418f, 1.043173f, 1.0521657f, 1.0611587f, 1.0701516f, 1.0791444f, 1.0881373f, 1.0971302f, 1.106123f, 1.1151159f, 1.1241088f, 1.1331016f, 1.1420945f, 1.1510874f, 1.1600802f, 1.1690731f, 1.178066f, 1.1870588f, 1.1960517f, 1.2050446f, 1.2140374f, 1.2230303f, 1.2320232f, 1.241016f, 1.250009f, 1.2590019f, 1.2679946f, 1.2769876f, 1.2859805f, 1.2949733f, 1.3039662f, 1.3129591f, 1.3219519f, 1.3309448f, 1.3399377f, 1.3489305f, 1.3579234f, 1.3669163f, 1.3759091f, 1.384902f, 1.3938948f, 1.4028877f, 1.4118806f, 1.4208734f, 1.4298663f, 1.4388592f, 1.447852f, 1.4568449f, 1.4658378f, 1.4748306f, 1.4838235f, 1.4928164f, 1.5018092f, 1.5108021f, 1.5197951f, 1.5287879f, 1.5377808f, 1.5467737f, 1.5557665f, 1.5647594f, 1.5737523f, 1.5827451f, 1.591738f, 1.6007309f, 1.6097237f, 1.6187166f, 1.6277095f, 1.6367023f, 1.6456952f, 1.6546881f, 1.6636809f, 1.6726738f, 1.6816667f, 1.6906595f, 1.6996524f, 1.7086453f, 1.7176381f, 1.726631f, 1.735624f, 1.7446167f, 1.7536097f, 1.7626026f, 1.7715954f, 1.7805883f, 1.7895812f, 1.798574f, 1.8075669f, 1.8165598f, 1.8255526f, 1.8345455f, 1.8435384f, 1.8525312f, 1.8615241f, 1.870517f, 1.8795098f, 1.8885027f, 1.8974956f, 1.9064884f, 1.9154813f, 1.9244742f, 1.933467f, 1.94246f, 1.9514527f, 1.9604456f, 1.9694386f, 1.9784313f, 1.9874243f, 1.9964172f, 2.00541f, 2.0144029f, 2.0233958f, 2.0323887f, 2.0413816f, 2.0503743f, 2.0593672f, 2.06836f, 2.077353f, 2.086346f, 2.0953388f, 2.1043315f, 2.1133244f, 2.1223173f, 2.1313102f, 2.1403031f, 2.149296f, 2.1582887f, 2.1672816f, 2.1762745f, 2.1852674f, 2.1942604f, 2.2032533f, 2.212246f, 2.2212389f, 2.2302318f, 2.2392247f, 2.2482176f, 2.2572103f, 2.2662032f, 2.275196f, 2.284189f, 2.293182f, 2.3021748f, 2.3111675f, 2.3201604f, 2.3291533f, 2.3381462f, 2.3471391f, 2.356132f, 2.3651247f, 2.3741176f, 2.3831105f, 2.3921034f, 2.4010963f, 2.4100893f, 2.419082f, 2.4280748f, 2.4370677f, 2.4460607f, 2.4550536f, 2.4640465f, 2.4730392f, 2.482032f, 2.491025f, 2.500018f, 2.5090108f, 2.5180037f, 2.5269964f, 2.5359893f, 2.5449822f, 2.553975f, 2.562968f, 2.571961f, 2.5809536f, 2.5899465f, 2.5989394f, 2.6079323f, 2.6169252f, 2.6259181f, 2.6349108f, 2.6439037f, 2.6528966f, 2.6618896f, 2.6708825f, 2.6798754f, 2.688868f, 2.697861f, 2.7068539f, 2.7158468f, 2.7248397f, 2.7338326f, 2.7428253f, 2.7518182f, 2.760811f, 2.769804f, 2.778797f, 2.7877896f, 2.7967825f, 2.8057754f, 2.8147683f, 2.8237612f, 2.8327541f, 2.8417468f, 2.8507397f, 2.8597326f, 2.8687255f, 2.8777184f, 2.8867114f, 2.895704f, 2.904697f, 2.9136899f, 2.9226828f, 2.9316757f, 2.9406686f, 2.9496613f, 2.9586542f, 2.967647f, 2.97664f, 2.985633f, 2.9946258f, 3.0036185f, 3.0126114f, 3.0216043f, 3.0305972f, 3.0395901f, 3.048583f, 3.0575757f, 3.0665686f, 3.0755615f, 3.0845544f, 3.0935473f, 3.1025403f, 3.111533f, 3.1205258f, 3.1295187f, 3.1385117f, 3.1475046f, 3.1564975f, 3.1654902f, 3.174483f, 3.183476f, 3.192469f, 3.2014618f, 3.2104547f, 3.2194474f, 3.2284403f, 3.2374332f, 3.246426f, 3.255419f, 3.264412f, 3.2734046f, 3.2823975f, 3.2913904f, 3.3003833f, 3.3093762f, 3.318369f, 3.3273618f, 3.3363547f, 3.3453476f, 3.3543406f, 3.3633335f, 3.3723261f, 3.381319f, 3.390312f, 3.3993049f, 3.4082978f, 3.4172907f, 3.4262834f, 3.4352763f, 3.4442692f, 3.453262f, 3.462255f, 3.471248f, 3.4802406f, 3.4892335f, 3.4982264f, 3.5072193f, 3.5162122f, 3.5252051f, 3.5341978f, 3.5431907f, 3.5521836f, 3.5611765f, 3.5701694f, 3.5791624f, 3.588155f, 3.597148f, 3.6061409f, 3.6151338f, 3.6241267f, 3.6331196f, 3.6421123f, 3.6511052f, 3.660098f, 3.669091f, 3.678084f, 3.6870768f, 3.6960695f, 3.7050624f, 3.7140553f, 3.7230482f, 3.7320411f, 3.741034f, 3.7500267f, 3.7590196f, 3.7680125f, 3.7770054f, 3.7859983f, 3.7949913f, 3.803984f, 3.8129768f, 3.8219697f, 3.8309627f, 3.8399556f, 3.8489485f, 3.8579412f, 3.866934f, 3.875927f, 3.88492f, 3.8939128f, 3.9029055f, 3.9118984f, 3.9208913f, 3.9298842f, 3.938877f, 3.94787f, 3.9568627f, 3.9658556f, 3.9748485f, 3.9838414f, 3.9928343f, 4.0018272f, 4.01082f, 4.019813f, 4.0288057f, 4.0377984f, 4.0467916f, 4.055784f, 4.0647774f, 4.07377f, 4.082763f, 4.091756f, 4.1007485f, 4.1097417f, 4.1187344f, 4.1277275f, 4.13672f, 4.145713f, 4.154706f, 4.1636987f, 4.172692f, 4.1816845f, 4.1906776f, 4.1996703f, 4.208663f, 4.217656f, 4.226649f, 4.235642f, 4.2446346f, 4.2536273f, 4.2626204f, 4.271613f, 4.2806063f, 4.289599f, 4.298592f, 4.307585f, 4.3165774f, 4.3255706f, 4.3345633f, 4.3435564f, 4.352549f, 4.3615417f, 4.370535f, 4.3795276f, 4.3885207f, 4.3975134f, 4.4065065f, 4.415499f, 4.424492f, 4.433485f, 4.4424777f, 4.451471f, 4.4604635f, 4.469456f, 4.4784493f, 4.487442f };
int SpacialData::altitudeSize = sizeof(altitude) / sizeof(altitude[0]);
int SpacialData::rlonSize = sizeof(rlon) / sizeof(rlon[0]);
int SpacialData::rlatSize = sizeof(rlat) / sizeof(rlat[0]);

int SpacialData::getR(float a, float* arr, int start, int end) {
	int h = (start + end) >> 1;

	if (start == end)
		return -1;

	if (fabs(arr[h] - a) < .0000001f)
		return h;

	if (a < arr[h])
		return getR(a, arr, start, h);

	return getR(a, arr, h + 1, end);
}

int SpacialData::getAltitudeIndex(float a) {
	return getR(a, altitude, 0, altitudeSize);
};

int SpacialData::getRlonIndex(float a) {
	return getR(a, rlon, 0, rlonSize);
};

int SpacialData::getRlatIndex(float a) {
	return getR(a, rlat, 0, rlatSize);
};